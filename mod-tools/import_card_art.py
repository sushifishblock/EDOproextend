#!/usr/bin/env python3
"""Download missing card art into EDOPro's pics folder.

What it does
  1. Reads every card id from the card databases in your EDOPro install
     (expansions\\*.cdb and repositories\\*\\*.cdb).
  2. Works out which ids have no picture yet, looking in <root>\\pics and <root>\\pics.zip.
  3. Downloads the full card image (421x614 JPG, the largest the source offers) for each
     missing id and saves it as <root>\\pics\\<id>.jpg.

It never overwrites an existing picture, never touches pics.zip, and writes each file
under a temporary name first so a stopped run can't leave a broken image behind.
Ids the server doesn't have (custom, anime and unofficial cards) are remembered in
import_card_art.cache.json next to this script, so the next run doesn't ask for them again.

Usage
  python import_card_art.py --dry-run          show what is missing, download nothing
  python import_card_art.py                    download everything that is missing
  python import_card_art.py --only official    skip unofficial, rush, skill and goat cards
  python import_card_art.py --limit 50         stop after 50 downloads (good for a first test)
  python import_card_art.py --fallback-url https://pics.projectignis.org:2096/pics/{id}.jpg
                                                use the game's own server for cards the main source lacks
  python import_card_art.py --only official --upgrade
                                                also replace low resolution pictures (originals are backed up)

The default source is YGOPRODeck's image server. They ask people to download images once
and keep them locally rather than hotlinking, and to stay under 20 requests a second.
This script does exactly that and defaults to 8 requests a second.
"""
import argparse
import glob
import json
import os
import re
import shutil
import sqlite3
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

DEFAULT_ROOT = r'C:\ProjectIgnis'
DEFAULT_URL = 'https://images.ygoprodeck.com/images/cards/{id}.jpg'
CACHE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'import_card_art.cache.json')
USER_AGENT = 'EDOPro-art-importer/1.0 (personal use, local copy)'
# databases whose cards are official TCG/OCG cards; everything else is tried afterwards
OFFICIAL_DB_HINTS = ('cards.cdb', 'cards.delta.cdb', 'release-', 'prerelease-')
UNOFFICIAL_DB_HINTS = ('unofficial', 'rush', 'skills', 'goat')


def read_card_ids(root):
    """Returns {card id: name of the first database that contains it}."""
    paths = glob.glob(os.path.join(root, 'expansions', '*.cdb'))
    paths += glob.glob(os.path.join(root, 'repositories', '*', '*.cdb'))
    ids = {}
    for path in sorted(paths):
        try:
            con = sqlite3.connect('file:' + path.replace('\\', '/') + '?mode=ro', uri=True)
            try:
                for (card_id,) in con.execute('select id from datas'):
                    ids.setdefault(card_id, os.path.basename(path))
            finally:
                con.close()
        except sqlite3.Error as err:
            print(f'  skipped {path}: {err}')
    return ids


def existing_art(root):
    have = set()
    pics = os.path.join(root, 'pics')
    for name in os.listdir(pics):
        match = re.fullmatch(r'(\d+)\.(jpg|png)', name, re.I)
        if match:
            have.add(int(match.group(1)))
    zip_path = os.path.join(root, 'pics.zip')
    if os.path.isfile(zip_path):
        with zipfile.ZipFile(zip_path) as archive:
            for name in archive.namelist():
                match = re.fullmatch(r'(?:pics/)?(\d+)\.(jpg|png)', name, re.I)
                if match:
                    have.add(int(match.group(1)))
    return have


def is_official(db_name):
    name = db_name.lower()
    if any(hint in name for hint in UNOFFICIAL_DB_HINTS):
        return False
    return any(hint in name for hint in OFFICIAL_DB_HINTS)


class RateLimiter:
    """Allows at most `rate` request starts per second across all threads."""

    def __init__(self, rate):
        self.interval = 1.0 / rate
        self.lock = threading.Lock()
        self.next_time = 0.0

    def wait(self):
        with self.lock:
            now = time.monotonic()
            wait = max(0.0, self.next_time - now)
            self.next_time = max(now, self.next_time) + self.interval
        if wait:
            time.sleep(wait)


def fetch(url, limiter):
    """Returns ('ok', bytes) | ('missing', None) | ('error', message)."""
    last_error = 'unknown error'
    for attempt in range(4):
        limiter.wait()
        request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read()
            if data[:2] != b'\xff\xd8' or len(data) < 2000:
                return 'missing', None  # placeholder or error page, not a real JPEG
            return 'ok', data
        except urllib.error.HTTPError as err:
            if err.code in (403, 404, 410):
                return 'missing', None
            last_error = f'HTTP {err.code}'
            if err.code == 429:
                time.sleep(10 * (attempt + 1))
        except (urllib.error.URLError, TimeoutError, OSError) as err:
            last_error = str(err)
        time.sleep(2 * (attempt + 1))
    return 'error', last_error


def load_cache():
    try:
        with open(CACHE_PATH, encoding='utf-8') as handle:
            return set(json.load(handle).get('not_found', []))
    except (OSError, ValueError):
        return set()


def save_cache(not_found):
    with open(CACHE_PATH, 'w', encoding='utf-8') as handle:
        json.dump({'not_found': sorted(not_found)}, handle)


def jpeg_size(data):
    """(width, height) of a JPEG, or None if it can't be read."""
    if data[:2] != b'\xff\xd8':
        return None
    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2):
            height, width = struct.unpack('>HH', data[i + 5:i + 9])
            return width, height
        i += 2 + struct.unpack('>H', data[i + 2:i + 4])[0]
    return None


def folder_widths(root):
    """{card id: width in pixels (0 if unreadable)} for the jpgs in <root>\\pics."""
    widths = {}
    pics = os.path.join(root, 'pics')
    for name in os.listdir(pics):
        match = re.fullmatch(r'(\d+)\.jpg', name, re.I)
        if match:
            with open(os.path.join(pics, name), 'rb') as handle:
                size = jpeg_size(handle.read(65536))
            widths[int(match.group(1))] = size[0] if size else 0
    return widths


def main():
    parser = argparse.ArgumentParser(description='Download missing card art for EDOPro.')
    parser.add_argument('--root', default=DEFAULT_ROOT, help='EDOPro install folder (default: %(default)s)')
    parser.add_argument('--url', default=DEFAULT_URL, help='image URL template, {id} is replaced by the card id')
    parser.add_argument('--only', choices=('all', 'official'), default='all',
                        help='official: skip unofficial, rush, skill and goat cards')
    parser.add_argument('--fallback-url', default=None,
                        help='second image source used only for cards the main source does not have; these pictures '
                             'may be lower resolution, and they are never used to replace an existing picture')
    parser.add_argument('--upgrade', action='store_true',
                        help='also replace existing pictures narrower than --min-width (originals are backed up first)')
    parser.add_argument('--min-width', type=int, default=800,
                        help='pictures narrower than this count as low resolution (default: %(default)s)')
    parser.add_argument('--backup-dir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'backup', 'pics_replaced'),
                        help='where originals are copied before being replaced (default: %(default)s)')
    parser.add_argument('--rate', type=float, default=8.0, help='requests per second (default: %(default)s)')
    parser.add_argument('--workers', type=int, default=4, help='parallel downloads (default: %(default)s)')
    parser.add_argument('--limit', type=int, default=0, help='stop after this many downloads (0 = no limit)')
    parser.add_argument('--retry-not-found', action='store_true', help='ask again for ids the server did not have before')
    parser.add_argument('--dry-run', action='store_true', help='only report what would be done')
    args = parser.parse_args()

    pics = os.path.join(args.root, 'pics')
    if not os.path.isdir(pics):
        sys.exit(f'Could not find {pics}. Use --root to point at your EDOPro folder.')
    if args.rate > 20:
        sys.exit('Please keep --rate at 20 or below, the image server asks for that.')

    ids = read_card_ids(args.root)
    have = existing_art(args.root)
    widths = folder_widths(args.root)
    missing = [(card_id, db) for card_id, db in ids.items() if card_id not in have]
    have_count = len(ids) - len(missing)
    if args.only == 'official':
        missing = [(card_id, db) for card_id, db in missing if is_official(db)]
    # existing pictures in the pics folder that are low resolution (the game prefers this folder over pics.zip)
    low_res = []
    if args.upgrade:
        low_res = [(card_id, db) for card_id, db in ids.items()
                   if card_id in widths and widths[card_id] < args.min_width
                   and (args.only == 'all' or is_official(db))]
    not_found = set() if args.retry_not_found else load_cache()
    todo = [(card_id, db, False) for card_id, db in missing if str(card_id) not in not_found]
    todo += [(card_id, db, True) for card_id, db in low_res if str(card_id) not in not_found]
    todo.sort(key=lambda item: (not is_official(item[1]), item[0]))  # official cards first

    print(f'Card ids in your databases : {len(ids)}')
    print(f'Already have art           : {have_count}')
    print(f'Missing art{" (official only)" if args.only == "official" else ""}'.ljust(28) + f': {len(missing)}')
    if args.upgrade:
        print(f'Low resolution (< {args.min_width}px wide) : {len(low_res)}  (will be replaced, originals backed up)')
    print(f'  of those, known to be unavailable from earlier runs: {len(missing) + len(low_res) - len(todo)}')
    print(f'To try now                 : {len(todo)}')
    if args.dry_run or not todo:
        return

    todo = todo[:args.limit] if args.limit else todo
    limiter = RateLimiter(args.rate)
    new_not_found = set(not_found)
    counts = {'ok': 0, 'fallback': 0, 'upgraded': 0, 'skipped': 0, 'missing': 0, 'error': 0}
    lock = threading.Lock()
    started = time.monotonic()

    def work(item):
        card_id, _, upgrade = item
        target = os.path.join(pics, f'{card_id}.jpg')
        if not upgrade and os.path.exists(target):
            return
        status, payload = fetch(args.url.format(id=card_id), limiter)
        if status == 'missing' and args.fallback_url and not upgrade:
            status, payload = fetch(args.fallback_url.format(id=card_id), limiter)
            if status == 'ok':
                status = 'fallback'
        if status == 'fallback':
            temp = target + '.part'
            with open(temp, 'wb') as handle:
                handle.write(payload)
            os.replace(temp, target)
        if status == 'ok':
            new_size = jpeg_size(payload)
            if upgrade:
                # only replace when the new picture really is sharper than the old one
                if not new_size or new_size[0] < args.min_width or new_size[0] <= widths.get(card_id, 0):
                    status = 'skipped'
                else:
                    os.makedirs(args.backup_dir, exist_ok=True)
                    backup = os.path.join(args.backup_dir, f'{card_id}.jpg')
                    if not os.path.exists(backup):
                        shutil.copy2(target, backup)
                    status = 'upgraded'
            if status in ('ok', 'upgraded'):
                temp = target + '.part'
                with open(temp, 'wb') as handle:
                    handle.write(payload)
                os.replace(temp, target)
        with lock:
            counts[status] += 1
            if status == 'missing':
                new_not_found.add(str(card_id))
            done = sum(counts.values())
            if done % 100 == 0:
                elapsed = time.monotonic() - started
                print(f'  {done}/{len(todo)}  new {counts["ok"]}, from fallback {counts["fallback"]}, upgraded {counts["upgraded"]}, '
                      f'not available {counts["missing"]}, errors {counts["error"]}  ({elapsed / 60:.1f} min)', flush=True)
                save_cache(new_not_found)

    try:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            list(pool.map(work, todo))
    except KeyboardInterrupt:
        print('\nStopped. Pictures saved so far are kept.')
    finally:
        save_cache(new_not_found)

    print(f'Done. New pictures: {counts["ok"]}, from fallback source: {counts["fallback"]}, upgraded to higher resolution: {counts["upgraded"]}, '
          f'left as they were: {counts["skipped"]}, not available: {counts["missing"]}, errors: {counts["error"]}.')
    if counts['upgraded']:
        print(f'Replaced originals were copied to {args.backup_dir}')
    if counts['ok'] or counts['fallback'] or counts['upgraded']:
        print('Restart EDOPro to see the new art.')


if __name__ == '__main__':
    main()

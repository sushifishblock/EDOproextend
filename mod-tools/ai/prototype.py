#!/usr/bin/env python3
"""EDOPro natural-language card search prototype (stdlib + optional numpy).

Subcommands:
  python prototype.py build            # build corpus, embed all cards, write embeddings.f32 / ids.u32
  python prototype.py query "text"     # run the full pipeline for one query
  python prototype.py eval             # run the evaluation set, print results
Servers expected: chat on 127.0.0.1:8081 (qwen), embeddings on 127.0.0.1:8082 (nomic).
"""
import sys, os, re, json, sqlite3, struct, time, glob, urllib.request, array

try:
    import numpy as np
except Exception:
    np = None

HERE = os.path.dirname(os.path.abspath(__file__))
DELIV = os.path.join(HERE, "deliverables")
PI = r"C:\ProjectIgnis"
CHAT = "http://127.0.0.1:8081"
EMB = "http://127.0.0.1:8082"

# ---------------------------------------------------------------- constants
TYPE_BITS = [(0x1, "Monster"), (0x2, "Spell"), (0x4, "Trap")]
SUBTYPE_NAMES = {  # name used in schema -> bit
    "normal": 0x10, "effect": 0x20, "fusion": 0x40, "ritual": 0x80, "spirit": 0x200,
    "union": 0x400, "gemini": 0x800, "tuner": 0x1000, "synchro": 0x2000, "xyz": 0x800000,
    "pendulum": 0x1000000, "link": 0x4000000, "flip": 0x200000, "toon": 0x400000,
    "special_summon": 0x2000000,
}
SPELLTRAP_NAMES = {"normal": 0x0, "quick-play": 0x10000, "continuous": 0x20000, "equip": 0x40000,
                   "field": 0x80000, "ritual": 0x80, "counter": 0x100000}
RACES = {"Warrior": 0x1, "Spellcaster": 0x2, "Fairy": 0x4, "Fiend": 0x8, "Zombie": 0x10, "Machine": 0x20,
         "Aqua": 0x40, "Pyro": 0x80, "Rock": 0x100, "Winged Beast": 0x200, "Plant": 0x400, "Insect": 0x800,
         "Thunder": 0x1000, "Dragon": 0x2000, "Beast": 0x4000, "Beast-Warrior": 0x8000, "Dinosaur": 0x10000,
         "Fish": 0x20000, "Sea Serpent": 0x40000, "Reptile": 0x80000, "Psychic": 0x100000,
         "Divine-Beast": 0x200000, "Creator God": 0x400000, "Wyrm": 0x800000, "Cyberse": 0x1000000,
         "Illusion": 0x2000000}
ATTRS = {"EARTH": 1, "WATER": 2, "FIRE": 4, "WIND": 8, "LIGHT": 16, "DARK": 32, "DIVINE": 64}


# ---------------------------------------------------------------- card DB
def db_files():
    fs = [PI + r"\expansions\cards.cdb", PI + r"\repositories\delta-bagooska\cards.delta.cdb"]
    d = PI + r"\repositories\delta-bagooska"
    fs += sorted(glob.glob(d + r"\release-*.cdb"))
    fs += sorted(f for f in glob.glob(d + r"\prerelease-*.cdb") if "rush" not in f.lower())
    return fs


def load_cards():
    """Return dict id -> card dict (later files override)."""
    cards = {}
    for f in db_files():
        if not os.path.exists(f):
            continue
        con = sqlite3.connect("file:" + f.replace("\\", "/") + "?mode=ro", uri=True)
        q = ("select d.id,d.ot,d.alias,d.setcode,d.type,d.atk,d.def,d.level,d.race,d.attribute,d.category,"
             "t.name,t.desc from datas d join texts t on t.id=d.id")
        for r in con.execute(q):
            cards[r[0]] = dict(id=r[0], ot=r[1], alias=r[2], setcode=r[3], type=r[4], atk=r[5], df=r[6],
                               level=r[7], race=r[8], attr=r[9], cat=r[10], name=r[11], desc=r[12] or "")
        con.close()
    return cards


def corpus_cards(cards):
    out = [c for c in cards.values()
           if c["ot"] in (1, 2, 3) and not (c["type"] & 0x4000) and c["desc"].strip()]
    out.sort(key=lambda c: c["id"])
    best = {}                       # drop alternate-artwork duplicates (same name + same text)
    for c in out:
        k = (c["name"], c["desc"])
        if k not in best or (best[k]["alias"] and not c["alias"]):
            best[k] = c
    keep = {c["id"] for c in best.values()}
    return [c for c in out if c["id"] in keep]


def load_archetypes():
    """name(lower) -> list of setcodes, from strings.conf files."""
    arch = {}
    for f in [PI + r"\config\strings.conf", PI + r"\repositories\delta-bagooska\strings.conf"]:
        if not os.path.exists(f):
            continue
        for line in open(f, encoding="utf-8", errors="replace"):
            m = re.match(r"!setname\s+(0x[0-9a-fA-F]+)\s+(.+?)\s*(?:\t.*)?$", line.strip())
            if m:
                arch.setdefault(m.group(2).strip().lower(), set()).add(int(m.group(1), 16))
    return arch


def load_banlist(fname="OCG.lflist.conf"):
    """First list in the file: id -> 0 forbidden / 1 limited / 2 semi-limited."""
    res, seen = {}, False
    for line in open(PI + r"\repositories\lflists" + "\\" + fname, encoding="utf-8", errors="replace"):
        if line.startswith("!"):
            if seen:
                break
            seen = True
            continue
        m = re.match(r"(\d+)\s+(\d)", line)
        if m and seen:
            res[int(m.group(1))] = int(m.group(2))
    return res


def setcode_match(card_setcode, code):
    for i in range(4):
        g = (card_setcode >> (16 * i)) & 0xFFFF
        if g == 0:
            continue
        if (g & 0xFFF) == (code & 0xFFF) and (g & code) == code:
            return True
    return False


# ---------------------------------------------------------------- corpus template
def type_line(c):
    t = c["type"]
    if t & 0x1:
        race = next((n for n, b in RACES.items() if c["race"] & b), "")
        parts = [race]
        order = [(0x1000000, "Pendulum"), (0x40, "Fusion"), (0x2000, "Synchro"), (0x800000, "Xyz"),
                 (0x4000000, "Link"), (0x80, "Ritual"), (0x200, "Spirit"), (0x400, "Union"), (0x800, "Gemini"),
                 (0x200000, "Flip"), (0x400000, "Toon"), (0x1000, "Tuner"), (0x20, "Effect")]
        parts += [n for b, n in order if t & b]
        if t & 0x10 and not t & 0x20:
            parts.append("Normal")
        return " / ".join(p for p in parts if p) + " Monster"
    if t & 0x2:
        kind = next((n for n, b in [("Quick-Play", 0x10000), ("Continuous", 0x20000), ("Equip", 0x40000),
                                    ("Field", 0x80000), ("Ritual", 0x80)] if t & b), "Normal")
        return kind + " Spell"
    if t & 0x4:
        kind = next((n for n, b in [("Continuous", 0x20000), ("Counter", 0x100000)] if t & b), "Normal")
        return kind + " Trap"
    return "Card"


def stat(v):
    return "?" if v == -2 else str(v)


def stats_line(c):
    t = c["type"]
    if not t & 0x1:
        return ""
    attr = next((n for n, b in ATTRS.items() if c["attr"] & b), "")
    lv = c["level"] & 0xFF
    bits = [attr] if attr else []
    if t & 0x4000000:
        bits.append("Link %d" % lv)
        bits.append("ATK " + stat(c["atk"]))
    else:
        bits.append(("Rank %d" if t & 0x800000 else "Level %d") % lv)
        bits.append("ATK " + stat(c["atk"]) + " DEF " + stat(c["df"]))
    if t & 0x1000000:
        bits.append("Pendulum Scale %d" % ((c["level"] >> 24) & 0xFF))
    return ", ".join(bits)


TEMPLATE = os.environ.get("CORPUS_TEMPLATE", "A")


def corpus_text(c, variant=None):
    """The exact string embedded (see corpus_template.txt)."""
    v = variant or TEMPLATE
    desc = re.sub(r"[\r\n]+", " ", c["desc"]).strip()
    if v == "C":   # effect text only
        return "search_document: " + desc
    if v == "B":   # type line + effect text
        return "search_document: " + type_line(c) + "\n" + desc
    lines = ["search_document: " + c["name"], type_line(c)]   # A: full
    s = stats_line(c)
    if s:
        lines.append(s)
    lines.append(desc)
    return "\n".join(lines)


# ---------------------------------------------------------------- HTTP helpers
def post(url, body, timeout=300):
    req = urllib.request.Request(url, json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def embed(texts):
    r = post(EMB + "/v1/embeddings", {"input": texts, "model": "nomic"})
    return [d["embedding"] for d in sorted(r["data"], key=lambda d: d["index"])]


def l2(v):
    n = sum(x * x for x in v) ** 0.5 or 1.0
    return [x / n for x in v]


def build(batch=32, limit=None, outdir=None):
    outdir = outdir or DELIV
    os.makedirs(outdir, exist_ok=True)
    cards = corpus_cards(load_cards())
    if limit:
        cards = cards[:limit]
    print("corpus cards:", len(cards))
    texts = [corpus_text(c) for c in cards]
    # sort by length for efficient batching, then restore order
    order = sorted(range(len(texts)), key=lambda i: len(texts[i]))
    vecs = [None] * len(texts)
    t0 = time.time()
    for s in range(0, len(order), batch):
        idx = order[s:s + batch]
        out = embed([texts[i] for i in idx])
        for i, v in zip(idx, out):
            vecs[i] = l2(v)
        if (s // batch) % 20 == 0:
            print("  %d/%d  %.1fs" % (s, len(order), time.time() - t0), flush=True)
    dt = time.time() - t0
    print("embedded %d cards in %.1fs (%.1f cards/s)" % (len(cards), dt, len(cards) / dt))
    with open(os.path.join(outdir, "embeddings.f32"), "wb") as f:
        for v in vecs:
            f.write(array.array("f", v).tobytes())
    with open(os.path.join(outdir, "ids.u32"), "wb") as f:
        f.write(array.array("I", [c["id"] for c in cards]).tobytes())
    return dt


# ---------------------------------------------------------------- query pipeline
EMB_DIR = os.environ.get("EMB_DIR", DELIV)


class Index:
    def __init__(self):
        self.cards_all = load_cards()
        self.ids = array.array("I")
        self.ids.frombytes(open(os.path.join(EMB_DIR, "ids.u32"), "rb").read())
        raw = open(os.path.join(EMB_DIR, "embeddings.f32"), "rb").read()
        self.n = len(self.ids)
        self.d = len(raw) // 4 // self.n
        if np is not None:
            self.mat = np.frombuffer(raw, dtype="<f4").reshape(self.n, self.d)
        else:
            a = array.array("f"); a.frombytes(raw); self.mat = a
        self.cards = [self.cards_all[i] for i in self.ids]
        self.lower = [(c["name"] + "\n" + c["desc"]).lower() for c in self.cards]
        self.arch = load_archetypes()
        self.ban = load_banlist()

    def sims(self, qvec, rows):
        if np is not None:
            return (self.mat[rows] @ np.asarray(qvec, dtype="f4")).tolist()
        out = []
        for r in rows:
            base = r * self.d
            out.append(sum(self.mat[base + k] * qvec[k] for k in range(self.d)))
        return out


def rng_ok(v, lo, hi):
    return (lo is None or v >= lo) and (hi is None or v <= hi)


def passes(c, f, ix):
    t = c["type"]
    ct = f.get("card_type")
    if ct == "monster" and not t & 1: return False
    if ct == "spell" and not t & 2: return False
    if ct == "trap" and not t & 4: return False
    if ct in ("spell", "trap") and f.get("spell_trap_kinds"):
        ok = False
        for k in f["spell_trap_kinds"]:
            b = SPELLTRAP_NAMES.get(k)
            if b is None: continue
            if b == 0:
                ok |= not (t & 0x1F0000)
            else:
                ok |= bool(t & b)
        if not ok: return False
    if ct in (None, "monster") and (t & 1):
        sub = f.get("monster_types") or []
        for s in sub:
            b = SUBTYPE_NAMES.get(s)
            if b is None: continue
            if s == "normal":
                if not (t & 0x10): return False
            elif not t & b: return False
        rs = f.get("races") or []
        if rs and not any(c["race"] & RACES.get(r, 0) for r in rs): return False
        at = f.get("attributes") or []
        if at and not any(c["attr"] & ATTRS.get(a, 0) for a in at): return False
        lv = c["level"] & 0xFF
        L = f.get("level") or {}
        if (L.get("min") is not None or L.get("max") is not None):
            if t & 0x4000000 or not rng_ok(lv, L.get("min"), L.get("max")): return False
        R = f.get("rank") or {}
        if (R.get("min") is not None or R.get("max") is not None):
            if not t & 0x800000 or not rng_ok(lv, R.get("min"), R.get("max")): return False
        K = f.get("link_rating") or {}
        if (K.get("min") is not None or K.get("max") is not None):
            if not t & 0x4000000 or not rng_ok(lv, K.get("min"), K.get("max")): return False
        A = f.get("atk") or {}
        if (A.get("min") is not None or A.get("max") is not None):
            if c["atk"] < 0 or not rng_ok(c["atk"], A.get("min"), A.get("max")): return False
        D = f.get("def") or {}
        if (D.get("min") is not None or D.get("max") is not None):
            if t & 0x4000000 or c["df"] < 0 or not rng_ok(c["df"], D.get("min"), D.get("max")): return False
        S = f.get("pendulum_scale") or {}
        if (S.get("min") is not None or S.get("max") is not None):
            if not t & 0x1000000: return False
            sc = (c["level"] >> 24) & 0xFF
            sr = (c["level"] >> 16) & 0xFF
            if not (rng_ok(sc, S.get("min"), S.get("max")) or rng_ok(sr, S.get("min"), S.get("max"))): return False
        PS = f.get("pendulum_scales") or []  # scales the card must have (any of its two)
        if PS:
            if not t & 0x1000000: return False
            have = {(c["level"] >> 24) & 0xFF, (c["level"] >> 16) & 0xFF}
            if not any(x in have for x in PS): return False
    nm = (f.get("name_or_archetype") or "").strip().lower()
    if nm:
        ok = nm in c["name"].lower()
        if not ok:
            for code in ix.arch.get(nm, ()):
                if setcode_match(c["setcode"], code): ok = True; break
        if not ok:
            for an, codes in ix.arch.items():
                if nm in an or an in nm:
                    if any(setcode_match(c["setcode"], k) for k in codes): ok = True; break
        if not ok: return False
    bs = f.get("ban_status")
    if bs:
        st = ix.ban.get(c["id"], ix.ban.get(c["alias"] or -1))
        want = {"forbidden": 0, "limited": 1, "semi_limited": 2}.get(bs)
        if bs == "any_restricted":
            if st is None: return False
        elif st != want: return False
    return True


# ---------------------------------------------------------------- grounding / sanitising of the LLM filters
HEAD_SPLIT = re.compile(r"\b(that|which|who|whose|when|where|while|if|can|able|to|doing|do|does)\b")
RACE_RX = {n: re.compile(r"\b" + re.escape(n.lower()).replace("\\-", "[- ]").replace("\ ", "[- ]") + r"(?:s|es)?\b")
           for n in RACES}
RACE_RX["Fairy"] = re.compile(r"\bfair(?:y|ies)\b")
RACE_RX["Zombie"] = re.compile(r"\bzombies?\b")
RACE_RX["Dinosaur"] = re.compile(r"\bdinosaurs?\b|\bdinos?\b")
RACE_RX["Winged Beast"] = re.compile(r"\bwinged[- ]beasts?\b")
RACE_RX["Beast"] = re.compile(r"\bbeasts?\b(?!-warrior)")
RACE_RX["Warrior"] = re.compile(r"\bwarriors?\b(?<!beast warrior)(?<!beast-warrior)")
ATTR_RX = {a: re.compile(r"\b" + a.lower() + r"\b") for a in ATTRS}
MTYPE_RX = {"synchro": r"synchros?", "xyz": r"xyz", "fusion": r"fusions?", "link": r"links?(?![- ]?\d*\s*(?:rating))",
            "pendulum": r"pendulums?", "ritual": r"rituals?(?!\s+spells?)", "tuner": r"tuners?",
            "spirit": r"spirits?", "union": r"unions?", "gemini": r"gemini", "flip": r"flips?", "toon": r"toons?",
            "normal": r"normal\s+monsters?", "effect": r"effect\s+monsters?"}
MTYPE_RX = {k: re.compile(r"\b" + v + r"\b") for k, v in MTYPE_RX.items()}
KIND_RX = {"quick-play": r"quick[- ]?play", "continuous": r"continuous", "equip": r"equip",
           "field": r"field\s+spells?|field\s+magic", "ritual": r"ritual\s+spells?", "counter": r"counter\s+traps?"}
KIND_RX = {k: re.compile(r"\b" + v + r"\b") for k, v in KIND_RX.items()}
CT_RX = {"monster": re.compile(r"\bmonsters?\b"), "spell": re.compile(r"\bspells?\b|\bmagic\b"),
         "trap": re.compile(r"\btraps?\b")}
NUM_KW = {"level": r"level|lvl|lv|low[- ]level|high[- ]level|\bl\d", "rank": r"rank", "link_rating": r"link",
          "atk": r"atk|attack|power|strong", "def": r"def|defen[sc]e", "pendulum_scale": r"scale"}
CMP_WORDS = re.compile(r"more than|less than|over|under|above|below|higher than|lower than|greater than|fewer than|"
                       r"bigger|smaller|exceed|>|<")


def nonempty_range(r):
    return isinstance(r, dict) and any(isinstance(r.get(k), int) for k in ("min", "max"))


def sanitize_filters(f, text):
    """Drop hallucinated filter values (must be grounded in the query words), add obvious ones the model missed."""
    t = text.lower()
    m = HEAD_SPLIT.search(t)
    head = t[:m.start()] if m else t
    out = {}
    nm = (f.get("name_or_archetype") or "").strip()
    if nm and nm.lower() in t and nm.lower() not in {x.lower() for x in list(RACES) + list(ATTRS)}:
        out["name_or_archetype"] = nm
    nm_l = nm.lower() if "name_or_archetype" in out else ""
    # card type
    cts = [k for k, rx in CT_RX.items() if rx.search(head)]
    ct = f.get("card_type")
    if len(cts) == 1:
        ct = cts[0]
    elif ct not in cts:
        ct = None
    # monster-ish
    races = [r for r, rx in RACE_RX.items() if rx.search(head) and r.lower() not in nm_l]
    attrs = [a for a, rx in ATTR_RX.items() if rx.search(head) and a.lower() not in nm_l]
    mts = [k for k, rx in MTYPE_RX.items() if rx.search(head) and k not in nm_l]
    kinds = [k for k, rx in KIND_RX.items() if rx.search(head) and k not in nm_l]
    if "continuous" in nm_l or "equip" in nm_l:
        pass
    if races: out["races"] = races
    if attrs: out["attributes"] = attrs
    if mts: out["monster_types"] = mts
    if kinds:
        out["spell_trap_kinds"] = kinds
        if not ct:
            if kinds == ["counter"]: ct = "trap"
            elif "continuous" not in kinds: ct = "spell"
    if (races or attrs or mts) and not ct:
        ct = "monster"
    if ct: out["card_type"] = ct
    # numbers
    for key in ("level", "rank", "link_rating", "atk", "def", "pendulum_scale"):
        r = f.get(key)
        if not nonempty_range(r) or not re.search(NUM_KW[key], t):
            continue
        r = {k: v for k, v in r.items() if k in ("min", "max") and isinstance(v, int)}
        nums = {int(x) for x in re.findall(r"\d+", t.replace(",", ""))}
        slack = 1 if CMP_WORDS.search(t) else 0
        if key == "level" and re.search(r"low[- ]level", t) and r.get("max") == 4: pass
        elif key == "level" and re.search(r"high[- ]level", t) and r.get("min") == 7: pass
        else:
            ok = all(any(abs(v - n) <= slack for n in nums) or (v in (0,) and key in ("atk", "def")) for v in r.values())
            if not ok:
                continue
        if key in ("level", "rank", "link_rating") and "min" in r and "max" not in r:
            mm = re.search(r"(?:" + {"level": "level|lvl|lv", "rank": "rank", "link_rating": "link"}[key] +
                           r")[\s-]*" + str(r["min"]) + r"(?!\d)(.{0,12})", t)
            if mm and not re.search(r"or|\+|up|higher|more|above|least", mm.group(1)) and not re.search(
                    r"(least|over|above|more than|higher than|from)\s*$", t[:mm.start()]):
                r["max"] = r["min"]
        out[key] = r
    PS = [x for x in (f.get("pendulum_scales") or []) if isinstance(x, int) and x in {int(n) for n in re.findall(r"\d+", t)}]
    if PS and "scale" in t:
        out["pendulum_scales"] = PS
        out.setdefault("card_type", "monster")
        out.setdefault("monster_types", ["pendulum"])
    if f.get("ban_status") and re.search(r"banned|forbidden|limited|banlist|ban list|restricted", t):
        out["ban_status"] = f["ban_status"]
    return out


RELAX = [("name_or_archetype", "ban_status"), ("level", "rank", "link_rating", "atk", "def", "pendulum_scale", "pendulum_scales"),
         ("races", "attributes", "monster_types", "spell_trap_kinds")]

SORT_KEYS = {"atk_desc": ("atk", True), "atk_asc": ("atk", False), "def_desc": ("df", True),
             "level_desc": ("level", True), "level_asc": ("level", False)}
KEYWORD_BONUS = 0.03
MAX_KW_BONUS = 0.09


def chat_query(system, user, schema, max_tokens=500):
    user = user.strip()[:600] or "(empty)"
    body = {"messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
            "temperature": 0, "max_tokens": max_tokens, "cache_prompt": True,
            "response_format": {"type": "json_schema", "json_schema": {"name": "card_query", "strict": True, "schema": schema}}}
    r = post(CHAT + "/v1/chat/completions", body)
    return json.loads(r["choices"][0]["message"]["content"]), r


def run_query(ix, text, system, schema, top=10, verbose=True):
    t0 = time.time()
    q, r = chat_query(system, text, schema)
    t1 = time.time()
    f = sanitize_filters(q.get("filters") or {}, text)
    rows = [i for i, c in enumerate(ix.cards) if passes(c, f, ix)]
    relaxed = []
    for group in RELAX:
        if rows or not any(k in f for k in group):
            continue
        f = {k: v for k, v in f.items() if k not in group}
        relaxed.append(group[0])
        rows = [i for i, c in enumerate(ix.cards) if passes(c, f, ix)]
    eff = (q.get("effect_description") or "").strip()
    kws = [k.lower() for k in (q.get("text_keywords") or []) if k.strip()]
    res = []
    nmq = (f.get("name_or_archetype") or "").lower()
    if rows:
        sims = ix.sims(l2(embed(["search_query: " + eff])[0]), rows) if eff else [0.0] * len(rows)
        for row, s_ in zip(rows, sims):
            hits = sum(1 for k in kws if k in ix.lower[row])
            if eff:
                score = s_ + min(MAX_KW_BONUS, KEYWORD_BONUS * hits)
            else:  # pure filter query: exact name > prefix > shorter name
                nm_ = ix.cards[row]["name"].lower()
                score = (2.0 if nm_ == nmq else 1.0 if nm_.startswith(nmq) else 0.0) - 0.001 * len(nm_) if nmq else 0.0
            res.append((score, s_, hits, row))
        res.sort(key=lambda x: -x[0])
        sb = q.get("sort_by")
        if sb in SORT_KEYS and not eff:   # superlative sort only when there is no effect text to rank by
            key, desc_ = SORT_KEYS[sb]
            def val(r_):
                c_ = ix.cards[r_[3]]
                v_ = c_[key] & 0xFF if key == "level" else c_[key]
                return v_ if c_["type"] & 1 and v_ >= 0 else -1
            res.sort(key=(lambda r_: -val(r_)) if desc_ else (lambda r_: (val(r_) < 0, val(r_))))
    lim = q.get("result_limit")
    n = min(top, lim) if isinstance(lim, int) and lim > 0 else top
    return dict(query=q, used_filters=f, relaxed=relaxed, passed=len(rows), results=[(ix.cards[r[3]], r[0], r[1], r[2]) for r in res[:n]],
                llm_s=t1 - t0, total_s=time.time() - t0,
                tokens=r.get("usage", {}))


def load_prompt():
    return (open(os.path.join(DELIV, "system_prompt.txt"), encoding="utf-8").read(),
            json.load(open(os.path.join(DELIV, "schema.json"))))


def show(ix, text, out, top=10):
    print("=" * 100)
    print("QUERY:", text[:200])
    print("JSON :", json.dumps(out["query"], ensure_ascii=False))
    print("USED :", json.dumps(out["used_filters"]), "relaxed:", out["relaxed"])
    print("passed hard filters: %d / %d   (llm %.2fs, total %.2fs, tokens %s)" % (
        out["passed"], ix.n, out["llm_s"], out["total_s"], out["tokens"].get("completion_tokens")))
    for c, sc, sim, hits in out["results"][:top]:
        print("  %.3f (cos %.3f kw %d) %s [%s]" % (sc, sim, hits, c["name"], type_line(c)))
        if os.environ.get("SHOWTEXT"):
            print("        " + re.sub(r"\s+", " ", c["desc"])[:int(os.environ["SHOWTEXT"])])


# Rough automatic relevance checks (regex on card text) used only for tuning P@10.
REL = {
 0: r"(if|when) this card is added to your hand",
 2: r"negate",
 3: r"destroy [^.;]{0,70}(spell|trap)",
 4: r"special summon (this card|it) from (your|the) (gy|graveyard)|in your (gy|graveyard)[^.;]{0,60}special summon (this card|it)(?! from your hand)",
 5: r"banish",
 7: r"(can|may) attack (your opponent )?directly|attack(s)? (your opponent )?directly|direct attack",
 8: r"(your opponent|neither player|neither of you|players?) cannot special summon|cannot special summon",
 9: r"add [^.;]{0,60}(spell|magic)[^.;]{0,60}from your deck[^.;]{0,25}to your hand",
 11: r"negate[^.;]{0,100}(monster effect|effect of a monster|monster.s effect|effect monster)|negate the effect",
 12: r"attack (twice|[0-9] times)|additional attack|second attack|attack all|attacks? again",
 13: r"draw (1|2|[0-9]) cards?|draw",
 14: r"draw",
 15: r"cannot be destroyed|not be destroyed|indestructible|is not destroyed",
 16: r"send (the top )?[0-9]* ?cards?( from the top)? (of your deck )?to the (gy|graveyard)|send the top|from the top of your deck to the (gy|graveyard)",
 17: r"special summon (this card|it) from (your|the) (gy|graveyard)|in your (gy|graveyard)[^.;]{0,60}special summon (this card|it)|ritual summon[^.;]{0,40}(gy|graveyard)",
}


def rel_score(i, results, k=10):
    rx = REL.get(i)
    if not rx:
        return None
    hit = sum(1 for c, *_ in results[:k] if re.search(rx, re.sub(r"[\r\n]+", " ", c["desc"]).lower()))
    return hit / max(1, min(k, len(results)))

EVAL_QUERIES = [
    "dragon monsters that do stuff in hand when added",
    "level 4 or lower light warriors with at least 1800 atk",
    "quick-play spells that negate",
    "traps that destroy spell or trap cards",
    "cards that special summon themselves from the graveyard",
    "synchro monsters that can banish cards",
    "pendulum monsters with scale 1 and 8",
    "monsters that can attack directly",
    "cards that stop the opponent from special summoning",
    "low level fire monsters that search a spell card",
    "decent boss monsters for a rank 8 xyz deck",
    "counter traps that negate a monster effect",
    "equip spells that give a monster extra attacks",
    "link-2 cyberse monsters that draw a card",
    "forbidden cards that let you draw",
    "field spells that protect your monsters from destruction",
    "dark fiend monsters that mill the deck",
    "ritual monsters that can be summoned from the graveyard",
]
ROBUST_QUERIES = [
    "asdf qwerty zzzz 12345 !!!",
    "blue eyes",
    "Dark Magician",
    "dragon monsters",
    "light attribute spellcaster level 4",
    "",
    "what is the weather in paris and write me a poem about cats",
    ("I am looking for a monster that can be special summoned from my hand when the opponent controls "
     "a monster and I control none and which also gets a bonus when it attacks and can also protect "
     "my other monsters from being destroyed by battle or by card effects and which preferably has a "
     "high attack and is a dragon or a machine or maybe even a warrior and is not too high level ") * 4,
]

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "build":
        build(batch=int(sys.argv[2]) if len(sys.argv) > 2 else 32, outdir=sys.argv[3] if len(sys.argv) > 3 else None)
    elif cmd in ("query", "eval", "robust"):
        ix = Index()
        system, schema = load_prompt()
        qs = sys.argv[2:] if cmd == "query" else (EVAL_QUERIES if cmd == "eval" else ROBUST_QUERIES)
        precs = []
        for qi, q in enumerate(qs):
            try:
                out_ = run_query(ix, q, system, schema)
                show(ix, q, out_)
                if cmd == "eval":
                    pr = rel_score(qi, out_["results"])
                    if pr is not None:
                        precs.append(pr)
                        print("  P@10(regex) = %.1f" % pr)
            except Exception as e:
                print("ERROR", q[:60], repr(e))
        if precs:
            print("MEAN P@10 (regex-judged queries, n=%d): %.3f" % (len(precs), sum(precs) / len(precs)))
    else:
        print(__doc__)

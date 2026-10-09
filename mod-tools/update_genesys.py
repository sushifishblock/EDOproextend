"""Downloads the current Genesys point costs (via the YGOPRODeck API) into genesys_points.json.

Usage: python update_genesys.py [install folder]   (default C:\\ProjectIgnis64)
The game reads the file when it starts. Run this again after Konami updates the points list.
"""
import json
import os
import sys
import urllib.request

dest = sys.argv[1] if len(sys.argv) > 1 else r"C:\ProjectIgnis64"
url = "https://db.ygoprodeck.com/api/v7/cardinfo.php?format=genesys&misc=yes"
req = urllib.request.Request(url, headers={"User-Agent": "edopro-genesys-updater"})
with urllib.request.urlopen(req, timeout=300) as response:
    data = json.load(response)["data"]
points = {}
for card in data:
    value = (card.get("misc_info") or [{}])[0].get("genesys_points", 0)
    if value:
        points[str(card["id"])] = value
out = os.path.join(dest, "genesys_points.json")
with open(out, "w", encoding="utf-8") as f:
    json.dump({"cap": 100, "points": points}, f)
print(f"{len(points)} cards with points written to {out}")

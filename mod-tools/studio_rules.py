"""
Rarity rules for the Rarity Studio: "every card of this archetype / type / attribute gets this rarity".
Written to rarity_rules.json in the game folder; the game picks it up within a second. A rule only decides for cards that
have no rarity of their own (the first matching rule wins), so a card you gave a rarity by hand keeps it.
"""
import glob
import json
import os
import re
import sqlite3
import tkinter as tk
from tkinter import messagebox, ttk

PANEL = "#272a34"
BG = "#1b1d25"
FG = "#e8e8ee"

TYPES = [("Monster", 0x1), ("Spell", 0x2), ("Trap", 0x4), ("Normal", 0x10), ("Effect", 0x20), ("Fusion", 0x40), ("Ritual", 0x80),
         ("Spirit", 0x200), ("Union", 0x400), ("Gemini", 0x800), ("Tuner", 0x1000), ("Synchro", 0x2000), ("Quick-Play", 0x10000),
         ("Continuous", 0x20000), ("Equip", 0x40000), ("Field", 0x80000), ("Counter", 0x100000), ("Flip", 0x200000), ("Toon", 0x400000),
         ("Xyz", 0x800000), ("Pendulum", 0x1000000), ("Link", 0x4000000)]
ATTRIBUTES = [("EARTH", 0x1), ("WATER", 0x2), ("FIRE", 0x4), ("WIND", 0x8), ("LIGHT", 0x10), ("DARK", 0x20), ("DIVINE", 0x40)]
RACES = [("Warrior", 0x1), ("Spellcaster", 0x2), ("Fairy", 0x4), ("Fiend", 0x8), ("Zombie", 0x10), ("Machine", 0x20), ("Aqua", 0x40),
         ("Pyro", 0x80), ("Rock", 0x100), ("Winged Beast", 0x200), ("Plant", 0x400), ("Insect", 0x800), ("Thunder", 0x1000),
         ("Dragon", 0x2000), ("Beast", 0x4000), ("Beast-Warrior", 0x8000), ("Dinosaur", 0x10000), ("Fish", 0x20000),
         ("Sea Serpent", 0x40000), ("Reptile", 0x80000), ("Psychic", 0x100000), ("Divine-Beast", 0x200000), ("Creator God", 0x400000),
         ("Wyrm", 0x800000), ("Cyberse", 0x1000000), ("Illusion", 0x2000000)]


def rule_matches(rule, card):
    """The same test the game does (card = dict with type, attribute, level, race, setcodes, name)."""
    setcode = rule.get("setcode", 0)
    if setcode:
        base, sub = setcode & 0xFFF, setcode & 0xF000
        if not any((sc & 0xFFF) == base and (sc & 0xF000 & sub) == sub for sc in card["setcodes"]):
            return False
    all_bits = rule.get("type_all", 0)
    if (card["type"] & all_bits) != all_bits or (card["type"] & rule.get("type_none", 0)):
        return False
    if rule.get("attribute_any", 0) and not (card["attribute"] & rule["attribute_any"]):
        return False
    if rule.get("race_any", 0) and not (card["race"] & rule["race_any"]):
        return False
    if rule.get("level_min", 0) and card["level"] < rule["level_min"]:
        return False
    if rule.get("level_max", 0) and card["level"] > rule["level_max"]:
        return False
    name = rule.get("name", "")
    if name and name.lower() not in card["name"].lower():
        return False
    return True


def has_condition(rule):
    return any(rule.get(k) for k in ("setcode", "type_all", "type_none", "attribute_any", "race_any", "level_min", "level_max", "name"))


def load_archetypes(folder):
    """name -> setcode from the strings.conf files of the game (!setname 0x8 Elemental HERO)."""
    found = {}
    paths = [os.path.join(folder, "config", "strings.conf")] + glob.glob(os.path.join(folder, "repositories", "*", "strings.conf"))
    pattern = re.compile(r"^!setname\s+(0x[0-9a-fA-F]+)\s+([^\t\r\n]+)")
    for path in paths:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    match = pattern.match(line)
                    if match:
                        name = match.group(2).strip()
                        if name and name not in found:
                            found[name] = int(match.group(1), 16)
        except OSError:
            continue
    return found


def load_cards(folder):
    """Every card of the card databases in the game folder (to show how many cards a rule catches)."""
    cards = {}
    paths = glob.glob(os.path.join(folder, "*.cdb")) + glob.glob(os.path.join(folder, "expansions", "*.cdb")) + glob.glob(os.path.join(folder, "repositories", "*", "*.cdb")) + glob.glob(os.path.join(folder, "repositories", "*", "*", "*.cdb"))
    for path in paths:
        try:
            db = sqlite3.connect("file:%s?mode=ro" % path.replace("\\", "/"), uri=True)
            for code, setcode, ctype, level, race, attribute, name in db.execute(
                    "select d.id, d.setcode, d.type, d.level, d.race, d.attribute, t.name from datas d join texts t on d.id = t.id"):
                if code in cards or ctype & 0x4000:  # (tokens)
                    continue
                cards[code] = {"setcodes": [(setcode >> (16 * i)) & 0xFFFF for i in range(4) if (setcode >> (16 * i)) & 0xFFFF],
                               "type": ctype, "level": level & 0xFF, "race": race, "attribute": attribute, "name": name or ""}
            db.close()
        except Exception:
            continue
    return cards


class RulesWindow(tk.Toplevel):
    def __init__(self, studio):
        super().__init__(studio)
        self.studio = studio
        self.title("Rarity rules")
        self.configure(bg=BG)
        self.geometry("1020x720")
        self.loading = True
        self.rules = []
        self.current = None
        self.save_job = None
        self.archetypes = load_archetypes(studio.game_folder.get())
        self.arch_names = sorted(self.archetypes, key=str.lower)
        self.arch_by_code = {}
        for name, code in self.archetypes.items():
            self.arch_by_code.setdefault(code, name)
        self.cards = None
        self.chosen_setcode = 0
        self.rarity_ids = []
        self.build()
        self.read_file()
        self.loading = False
        self.refresh_list()
        if self.rules:
            self.select(0)

    # ------------------------------------------------------------------ ui
    def build(self):
        ttk.Label(self, text="Every card that fits a rule gets its rarity - unless you gave that card a rarity yourself. The first rule in the list that fits wins.",
                  style="Hint.TLabel", wraplength=980).pack(anchor="w", padx=12, pady=(10, 4))
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 6))
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        left = ttk.LabelFrame(body, text="Rules (top = first)", padding=8)
        left.grid(row=0, column=0, sticky="ns", padx=(0, 10))
        self.listbox = tk.Listbox(left, width=44, height=22, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111",
                                  font=("Segoe UI", 10), activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False)
        self.listbox.pack(fill="both", expand=True)
        self.listbox.bind("<<ListboxSelect>>", lambda _e: self.on_pick())
        row = ttk.Frame(left)
        row.pack(fill="x", pady=(8, 0))
        ttk.Button(row, text="Add rule", style="Accent.TButton", command=self.add_rule).pack(side="left")
        ttk.Button(row, text="Delete", command=self.delete_rule).pack(side="left", padx=6)
        ttk.Button(row, text="▲", width=3, command=lambda: self.move(-1)).pack(side="left")
        ttk.Button(row, text="▼", width=3, command=lambda: self.move(1)).pack(side="left", padx=(4, 0))

        right = ttk.LabelFrame(body, text="The selected rule", padding=10)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)

        top = ttk.Frame(right)
        top.grid(row=0, column=0, sticky="ew")
        ttk.Label(top, text="Gives this rarity:").pack(side="left")
        self.rarity_box = ttk.Combobox(top, state="readonly", width=34)
        self.rarity_box.pack(side="left", padx=8)
        self.rarity_box.bind("<<ComboboxSelected>>", lambda _e: self.on_edit())

        arch = ttk.LabelFrame(right, text="Archetype (the group a card belongs to)", padding=6)
        arch.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        arch.columnconfigure(1, weight=1)
        ttk.Label(arch, text="Search:").grid(row=0, column=0, sticky="w")
        self.filter_var = tk.StringVar()
        entry = ttk.Entry(arch, textvariable=self.filter_var)
        entry.grid(row=0, column=1, sticky="ew", padx=6)
        self.filter_var.trace_add("write", lambda *_: self.fill_archetypes())
        self.arch_label = ttk.Label(arch, text="Any archetype")
        self.arch_label.grid(row=0, column=2, sticky="e")
        ttk.Button(arch, text="Clear", width=6, command=lambda: self.pick_archetype(0)).grid(row=0, column=3, padx=(6, 0))
        self.arch_list = tk.Listbox(arch, height=5, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111", font=("Segoe UI", 10),
                                    activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False)
        self.arch_list.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(4, 0))
        self.arch_list.bind("<<ListboxSelect>>", lambda _e: self.on_archetype_click())
        self.shown_archetypes = []
        self.fill_archetypes()

        lists = ttk.Frame(right)
        lists.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        self.type_all = self.make_list(lists, 0, "Must be (all)", [n for n, _ in TYPES])
        self.type_none = self.make_list(lists, 1, "Must not be", [n for n, _ in TYPES])
        self.attr_list = self.make_list(lists, 2, "Attribute (any)", [n for n, _ in ATTRIBUTES])
        self.race_list = self.make_list(lists, 3, "Race (any)", [n for n, _ in RACES])

        extra = ttk.Frame(right)
        extra.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        ttk.Label(extra, text="Name contains:").pack(side="left")
        self.name_var = tk.StringVar()
        name_entry = ttk.Entry(extra, textvariable=self.name_var, width=22)
        name_entry.pack(side="left", padx=(6, 16))
        name_entry.bind("<KeyRelease>", lambda _e: self.on_edit())
        ttk.Label(extra, text="Level / Rank from").pack(side="left")
        self.level_min = tk.Spinbox(extra, from_=0, to=13, width=4, command=self.on_edit, bg=PANEL, fg=FG, buttonbackground="#3a3f50", highlightthickness=0, relief="flat")
        self.level_min.pack(side="left", padx=4)
        ttk.Label(extra, text="to").pack(side="left")
        self.level_max = tk.Spinbox(extra, from_=0, to=13, width=4, command=self.on_edit, bg=PANEL, fg=FG, buttonbackground="#3a3f50", highlightthickness=0, relief="flat")
        self.level_max.pack(side="left", padx=4)
        ttk.Label(extra, text="(0 = no limit)", style="Hint.TLabel").pack(side="left", padx=6)
        for box in (self.level_min, self.level_max):
            box.bind("<KeyRelease>", lambda _e: self.on_edit())

        self.result = ttk.Label(right, text="", wraplength=640, justify="left")
        self.result.grid(row=4, column=0, sticky="w", pady=(12, 0))
        self.status = ttk.Label(self, text="", style="Hint.TLabel")
        self.status.pack(fill="x", padx=12, pady=(0, 8))

    def make_list(self, parent, column, title, names):
        frame = ttk.Frame(parent)
        frame.grid(row=0, column=column, sticky="nsew", padx=(0, 8))
        ttk.Label(frame, text=title, style="Hint.TLabel").pack(anchor="w")
        inner = ttk.Frame(frame)
        inner.pack(fill="both", expand=True)
        box = tk.Listbox(inner, selectmode="multiple", width=14, height=9, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111",
                         font=("Segoe UI", 9), activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False)
        scroll = ttk.Scrollbar(inner, orient="vertical", command=box.yview)
        box.config(yscrollcommand=scroll.set)
        box.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        for name in names:
            box.insert("end", name)
        box.bind("<<ListboxSelect>>", lambda _e: self.on_edit())
        return box

    # -------------------------------------------------------------- files
    def file_path(self):
        return os.path.join(self.studio.game_folder.get(), "rarity_rules.json")

    def read_file(self):
        try:
            with open(self.file_path(), "r", encoding="utf-8") as f:
                data = json.load(f)
            self.rules = [r for r in data.get("rules", []) if isinstance(r, dict) and isinstance(r.get("rarity"), str)]
        except FileNotFoundError:
            self.rules = []
        except Exception as e:
            messagebox.showerror("Rarity Studio", "rarity_rules.json could not be read:\n%s" % e)
            self.rules = []

    def schedule_save(self):
        if self.save_job:
            self.after_cancel(self.save_job)
        self.save_job = self.after(300, self.save_now)

    def save_now(self):
        self.save_job = None
        folder = self.studio.game_folder.get()
        if not os.path.isdir(folder):
            self.set_status("The game folder does not exist.")
            return
        try:
            target = self.file_path()
            if not self.rules and not os.path.exists(target):
                return
            tmp = target + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"version": 1, "rules": self.rules}, f, indent=1)
            os.replace(tmp, target)
            self.set_status("Saved at %s - the game picks it up within a second." % __import__("time").strftime("%H:%M:%S"))
        except Exception as e:
            self.set_status("Could not save: %s" % e)

    def set_status(self, text):
        self.status.config(text=text)
        self.studio.set_status(text)

    # ------------------------------------------------------------- display
    def rarity_choices(self):
        ids = []
        labels = []
        for rid in ["SR", "UR", "SCR", "GR", "PR", "GST"] + ["C%d" % i for i in range(1, 9)]:
            if rid.startswith("C") and rid not in self.studio.data:
                continue
            ids.append(rid)
            name = self.studio.data.get(rid, {}).get("name") or {"SR": "Super Rare (SR)", "UR": "Ultra Rare (UR)", "SCR": "Secret Rare (ScR)", "GR": "Gold Rare (GR)",
                                                                  "PR": "Prismatic Secret (PR)", "GST": "Ghost Rare (Gst)"}.get(rid, "Custom " + rid[1:])
            labels.append(name)
        return ids, labels

    def describe(self, rule):
        parts = []
        if rule.get("setcode"):
            parts.append(self.arch_by_code.get(rule["setcode"], "archetype 0x%x" % rule["setcode"]))
        parts += [n for n, bit in TYPES if rule.get("type_all", 0) & bit]
        parts += ["not " + n for n, bit in TYPES if rule.get("type_none", 0) & bit]
        parts += ["/".join(n for n, bit in ATTRIBUTES if rule.get("attribute_any", 0) & bit)] if rule.get("attribute_any") else []
        parts += ["/".join(n for n, bit in RACES if rule.get("race_any", 0) & bit)] if rule.get("race_any") else []
        if rule.get("level_min") or rule.get("level_max"):
            parts.append("level %s-%s" % (rule.get("level_min") or "any", rule.get("level_max") or "any"))
        if rule.get("name"):
            parts.append('name has "%s"' % rule["name"])
        what = ", ".join(parts) if parts else "(no condition yet)"
        return "%s  →  %s" % (what, rule["rarity"])

    def refresh_list(self):
        keep = self.current
        self.listbox.delete(0, "end")
        for i, rule in enumerate(self.rules):
            self.listbox.insert("end", "%d. %s" % (i + 1, self.describe(rule)))
        if keep is not None and 0 <= keep < len(self.rules):
            self.listbox.selection_set(keep)

    def fill_archetypes(self):
        text = self.filter_var.get().strip().lower()
        names = [n for n in self.arch_names if text in n.lower()][:300] if self.arch_names else []
        self.shown_archetypes = names
        self.arch_list.delete(0, "end")
        for name in names:
            self.arch_list.insert("end", name)
        if not self.arch_names:
            self.arch_list.insert("end", "(no archetype names found in the game folder: config/strings.conf)")

    def on_archetype_click(self):
        sel = self.arch_list.curselection()
        if sel and sel[0] < len(self.shown_archetypes):
            self.pick_archetype(self.archetypes[self.shown_archetypes[sel[0]]])

    def pick_archetype(self, code):
        self.chosen_setcode = code
        self.arch_label.config(text=self.arch_by_code.get(code, "Any archetype") if code else "Any archetype")
        self.on_edit()

    # ----------------------------------------------------------- selecting
    def select(self, index):
        self.current = index
        self.listbox.selection_clear(0, "end")
        self.listbox.selection_set(index)
        self.load_editor()

    def on_pick(self):
        sel = self.listbox.curselection()
        if sel:
            self.select(sel[0])

    def load_editor(self):
        self.loading = True
        rule = self.rules[self.current] if self.current is not None and self.current < len(self.rules) else None
        ids, labels = self.rarity_choices()
        self.rarity_ids = ids
        self.rarity_box["values"] = labels
        if rule is None:
            self.loading = False
            self.result.config(text="Add a rule on the left.")
            return
        self.rarity_box.current(ids.index(rule["rarity"]) if rule["rarity"] in ids else 0)
        self.chosen_setcode = rule.get("setcode", 0)
        self.arch_label.config(text=self.arch_by_code.get(self.chosen_setcode, "archetype 0x%x" % self.chosen_setcode) if self.chosen_setcode else "Any archetype")

        def fill(box, table, key):
            box.selection_clear(0, "end")
            for i, (_n, bit) in enumerate(table):
                if rule.get(key, 0) & bit:
                    box.selection_set(i)

        fill(self.type_all, TYPES, "type_all")
        fill(self.type_none, TYPES, "type_none")
        fill(self.attr_list, ATTRIBUTES, "attribute_any")
        fill(self.race_list, RACES, "race_any")
        self.name_var.set(rule.get("name", ""))
        for box, key in ((self.level_min, "level_min"), (self.level_max, "level_max")):
            box.delete(0, "end")
            box.insert(0, str(rule.get(key, 0)))
        self.loading = False
        self.update_result()

    def gather(self):
        def mask(box, table):
            value = 0
            for i in box.curselection():
                value |= table[i][1]
            return value

        def number(box):
            try:
                return max(0, min(13, int(box.get())))
            except ValueError:
                return 0

        rule = {"rarity": self.rarity_ids[self.rarity_box.current()] if self.rarity_ids and self.rarity_box.current() >= 0 else "SR"}
        rule["setcode"] = self.chosen_setcode
        rule["type_all"] = mask(self.type_all, TYPES)
        rule["type_none"] = mask(self.type_none, TYPES)
        rule["attribute_any"] = mask(self.attr_list, ATTRIBUTES)
        rule["race_any"] = mask(self.race_list, RACES)
        rule["level_min"] = number(self.level_min)
        rule["level_max"] = number(self.level_max)
        rule["name"] = self.name_var.get().strip()
        return {k: v for k, v in rule.items() if v or k == "rarity"}

    def on_edit(self):
        if self.loading or self.current is None or self.current >= len(self.rules):
            return
        self.rules[self.current] = self.gather()
        self.refresh_list()
        self.update_result()
        self.schedule_save()

    def update_result(self):
        if self.current is None or self.current >= len(self.rules):
            return
        rule = self.rules[self.current]
        if not has_condition(rule):
            self.result.config(text="This rule has no condition yet, so it does nothing. Pick an archetype, a type, an attribute...")
            return
        if self.cards is None:
            self.set_status("Reading the card databases...")
            self.update_idletasks()
            self.cards = load_cards(self.studio.game_folder.get())
            self.set_status("")
        if not self.cards:
            self.result.config(text="(No card database found in the game folder, so the number of cards cannot be shown.)")
            return
        earlier = self.rules[:self.current]
        caught = [c["name"] for code, c in self.cards.items() if rule_matches(rule, c) and not any(rule_matches(e, c) for e in earlier if has_condition(e))]
        total = sum(1 for c in self.cards.values() if rule_matches(rule, c))
        sample = ", ".join(sorted(caught)[:8])
        text = "This rule gives the rarity to %d cards" % len(caught)
        if total != len(caught):
            text += " (%d fit it, the rest are taken by rules above)" % total
        self.result.config(text=text + (":\n" + sample + (" ..." if len(caught) > 8 else "") if caught else "."))

    # -------------------------------------------------------------- editing
    def add_rule(self):
        ids, _ = self.rarity_choices()
        self.rules.append({"rarity": ids[0] if ids else "SR"})
        self.refresh_list()
        self.select(len(self.rules) - 1)
        self.schedule_save()

    def delete_rule(self):
        if self.current is None or self.current >= len(self.rules):
            return
        del self.rules[self.current]
        self.current = min(self.current, len(self.rules) - 1) if self.rules else None
        self.refresh_list()
        if self.current is not None:
            self.select(self.current)
        else:
            self.load_editor()
        self.schedule_save()

    def move(self, delta):
        new = (self.current if self.current is not None else -1) + delta
        if self.current is None or not 0 <= new < len(self.rules):
            return
        self.rules[self.current], self.rules[new] = self.rules[new], self.rules[self.current]
        self.current = new
        self.refresh_list()
        self.update_result()
        self.schedule_save()


def open_rules_window(studio):
    existing = getattr(studio, "rules_window", None)
    if existing is not None and existing.winfo_exists():
        existing.lift()
        return
    studio.rules_window = RulesWindow(studio)


def collect_rules(folder):
    """rarity_rules.json as a dict for a rarity pack (an empty list when there are no rules: that clears the friend's rules)."""
    try:
        with open(os.path.join(folder, "rarity_rules.json"), "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict) and isinstance(data.get("rules"), list):
            return data
    except Exception:
        pass
    return {"version": 1, "rules": []}

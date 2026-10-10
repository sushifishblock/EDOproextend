"""
Rarity Studio - make your own card rarity effects for the EDOPro mod.

Run it with:   python rarity_studio.py        (no extra packages needed, only Python 3 with Tk)

1. Start the game once with the new version (it writes rarities_builtin.json into its folder).
2. Open this program, pick your game folder (the one with EDOPro.exe).
3. Choose a rarity on the left, then add / change layers and move the sliders.
   Turn on "Apply to the game automatically" and the game updates within a second while it is running
   (open the deck editor or the card browser and hover a card with that rarity).
4. Custom rarities (Custom 1-8) show up in the rarity dropdown of the game next to the normal ones.

The result is one file, custom_rarities.json, in the game folder. "Export..." saves a copy to share,
"Import..." loads one from a friend.
"""
import copy
import json
import os
import shutil
import sys
import time
import zipfile
import tkinter as tk
from tkinter import colorchooser, filedialog, messagebox, ttk

try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rarity_preview
    from PIL import Image, ImageTk
    PREVIEW_ERROR = None
except Exception as error:  # Pillow / numpy missing
    rarity_preview = None
    PREVIEW_ERROR = str(error)

SETTINGS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rarity_studio_settings.json")
DEFAULT_GAME_FOLDER = r"C:\ProjectIgnis64"

PATTERNS = [
    ("FLAT", "Flat color", "The whole area is tinted with one color."),
    ("BEAM", "Moving light beam", "A soft diagonal beam of light that sweeps across."),
    ("GLITTER", "Glitter", "Small sparkles that twinkle."),
    ("CLOUD", "Soft clouds", "Slow drifting cloudy haze."),
    ("GLOW", "Round glow", "A soft round glow."),
    ("RAYS", "Rays", "Light rays fanning out."),
    ("VIGNETTE", "Edge glow", "A glow along the edges of the area."),
    ("HATCH", "Fine lines", "Fine diagonal lines (like Secret Rare)."),
    ("HATCHBAND", "Fine lines in a band", "Diagonal lines only inside a moving band."),
    ("WEAVE", "Weave", "Warp and weft thread weave (like Prismatic)."),
    ("WEAVEBAND", "Weave in a band", "The weave only inside a moving band."),
    ("SHEEN", "Soft sheen", "A broad soft shine."),
    ("LINES", "Lines", "Plain parallel lines."),
    ("DOTS", "Dots", "A grid of dots."),
    ("VDASH", "Vertical dashes", "Short vertical dashes."),
    ("VDASHBAND", "Dashes in a band", "Vertical dashes only inside a band."),
]
PATTERN_LABEL = {key: label for key, label, _ in PATTERNS}
PATTERN_HINT = {key: hint for key, _, hint in PATTERNS}
REGIONS = [("CARD", "Whole card"), ("ART", "Picture only"), ("NAME", "Name bar"), ("BORDER", "Card border")]
REGION_LABEL = dict(REGIONS)

BUILTIN_IDS = ["SR", "UR", "SCR", "GR", "PR", "GST"]
BUILTIN_NAMES = {"SR": "Super Rare (SR)", "UR": "Ultra Rare (UR)", "SCR": "Secret Rare (ScR)", "GR": "Gold Rare (GR)",
                 "PR": "Prismatic Secret (PR)", "GST": "Ghost Rare (Gst)"}
CUSTOM_IDS = ["C%d" % i for i in range(1, 9)]
ALL_IDS = BUILTIN_IDS + CUSTOM_IDS

NEW_LAYER = {"pattern": "BEAM", "region": "ART", "color": "80FFFFFF", "tile": 2.0, "vx": 0.12, "vy": 0.0,
             "hue_center": -1.0, "hue_amp": 0.0, "hue_rate": 0.0, "rainbow": False,
             "pulse_amp": 0.0, "pulse_rate": 0.0, "sat": 0.45, "enabled": True}

TEMPLATES = {
    "Light sweep": dict(NEW_LAYER),
    "Glitter": dict(NEW_LAYER, pattern="GLITTER", color="A0FFFFFF", tile=0.7, vx=0.03, vy=-0.05),
    "Rainbow haze": dict(NEW_LAYER, pattern="CLOUD", color="70FFFFFF", tile=1.2, vx=0.04, vy=0.02, rainbow=True, hue_rate=0.1, sat=0.6),
    "Gold wash": dict(NEW_LAYER, pattern="FLAT", color="30FFD24A", tile=1.0, vx=0.0, vy=0.0, pulse_amp=0.4, pulse_rate=0.3),
    "Glowing border": dict(NEW_LAYER, pattern="BEAM", region="BORDER", color="C0FFFFFF", tile=1.2, vx=0.35, rainbow=True, hue_rate=0.3, sat=0.7),
    "Fine lines band": dict(NEW_LAYER, pattern="HATCHBAND", color="A0FFFFFF", tile=0.8, vx=0.14, vy=0.05, rainbow=True, hue_rate=0.2),
    "Weave": dict(NEW_LAYER, pattern="WEAVE", region="CARD", color="B0FFFFFF", tile=0.5, vx=0.02, vy=0.01),
    "Edge glow": dict(NEW_LAYER, pattern="VIGNETTE", color="80B0D0FF", tile=0.77, vx=0.0, vy=0.0, pulse_amp=0.3, pulse_rate=0.35),
}

def L(pattern, region, color, tile, vx=0.0, vy=0.0, **kw):
    layer = dict(NEW_LAYER, pattern=pattern, region=region, color=color, tile=tile, vx=vx, vy=vy)
    layer.update(kw)
    return layer


def rainbow(rate, sat=0.5):
    return dict(rainbow=True, hue_rate=rate, sat=sat)


def drift(center, amp, rate, sat=0.5):
    return dict(hue_center=center, hue_amp=amp, hue_rate=rate, sat=sat)


# Ready-made foils. They only use what the game can draw; nothing here is added anywhere until you add it yourself.
PRESETS = [
    ("Holo Dash (the example foil)", "Fine vertical rainbow dashes over the whole card, sweeping dash bands, a pastel glow, sparkles and a soft rainbow frame.", [
        L("VDASH", "CARD", "38FFFFFF", 0.30, 0.02, 0.0),
        L("VDASHBAND", "CARD", "A8FFFFFF", 1.0, 0.12, 0.0),
        L("VDASHBAND", "ART", "70FFFFFF", 1.7, -0.09, 0.0),
        L("BEAM", "ART", "48FFFFFF", 2.6, 0.14, 0.0, **rainbow(0.08, 0.35)),
        L("FLAT", "CARD", "16FFC8E6", 1.0, pulse_amp=0.5, pulse_rate=0.25),
        L("GLITTER", "ART", "90FFFFFF", 0.6, 0.02, -0.04),
        L("BEAM", "NAME", "50FFFFFF", 1.2, 0.3, 0.0, **rainbow(0.2, 0.3)),
        L("BEAM", "BORDER", "C8FFFFFF", 1.0, 0.35, 0.0, **rainbow(0.3, 0.45)),
    ]),
    ("Aurora Veil", "Slow green-teal-purple curtains over the picture with a cool glow at the edges.", [
        L("CLOUD", "ART", "78FFFFFF", 1.3, 0.03, 0.02, **drift(0.5, 0.18, 0.07, 0.6)),
        L("BEAM", "ART", "58FFFFFF", 2.4, 0.12, 0.0, **drift(0.45, 0.2, 0.09, 0.55)),
        L("VIGNETTE", "ART", "60A0FFE0", 0.77, 0.0, 0.0, pulse_amp=0.35, pulse_rate=0.3),
        L("GLITTER", "ART", "80FFFFFF", 0.7, 0.02, -0.05),
        L("BEAM", "BORDER", "B0C8FFF0", 1.0, 0.3, 0.0, **drift(0.5, 0.15, 0.1, 0.5)),
    ]),
    ("Gold Rush", "Warm gold wash with sweeping fine gold lines, rays and a golden frame.", [
        L("FLAT", "ART", "1CFFD24A", 1.0),
        L("HATCHBAND", "ART", "D0FFD860", 0.8, 0.14, 0.05),
        L("HATCHBAND", "ART", "90FFC040", 0.6, -0.10, 0.07),
        L("RAYS", "ART", "50FFE070", 1.8, 0.10, 0.0),
        L("GLITTER", "ART", "A0FFD24A", 0.6, 0.02, -0.04),
        L("VIGNETTE", "ART", "58FFD24A", 0.77, 0.0, 0.0, pulse_amp=0.3, pulse_rate=0.3),
        L("BEAM", "BORDER", "D0FFD24A", 1.0, 0.3, 0.0),
    ]),
    ("Cosmic Weave", "A fine woven rainbow over a deep blue-violet haze with silver sparkles.", [
        L("CLOUD", "ART", "70FFFFFF", 1.2, 0.03, 0.02, **drift(0.68, 0.12, 0.08, 0.6)),
        L("WEAVE", "CARD", "70FFFFFF", 0.5, 0.02, 0.01),
        L("WEAVEBAND", "CARD", "C0FFFFFF", 0.5, 0.12, 0.05),
        L("GLITTER", "ART", "A0DCE6FF", 0.5, 0.02, -0.04),
        L("GLITTER", "ART", "80FFFFFF", 0.8, -0.02, -0.03),
        L("BEAM", "BORDER", "C0B0B8FF", 1.0, 0.3, 0.0, **drift(0.68, 0.1, 0.1, 0.6)),
    ]),
    ("Neon Grid", "Rainbow dots over the card and neon scan lines drifting across the picture.", [
        L("DOTS", "CARD", "58FFFFFF", 0.35, 0.0, 0.03),
        L("LINES", "ART", "60FFFFFF", 0.5, 0.0, 0.1),
        L("BEAM", "ART", "50FFFFFF", 2.2, 0.0, 0.2, **rainbow(0.12, 0.7)),
        L("VIGNETTE", "ART", "50FF40FF", 0.77, 0.0, 0.0, pulse_amp=0.5, pulse_rate=0.5),
        L("BEAM", "BORDER", "D0FFFFFF", 1.0, 0.4, 0.0, **rainbow(0.4, 0.9)),
    ]),
    ("Ice Crystal", "Pale blue frost: cold glow at the edges, drifting white mist and icy sparkles.", [
        L("FLAT", "ART", "18A0D8FF", 1.0, pulse_amp=0.4, pulse_rate=0.2),
        L("CLOUD", "ART", "70E0F4FF", 1.0, 0.03, 0.01),
        L("BEAM", "ART", "70FFFFFF", 2.6, 0.12, 0.0),
        L("VIGNETTE", "ART", "70A0E0FF", 0.77, 0.0, 0.0, pulse_amp=0.3, pulse_rate=0.3),
        L("GLITTER", "ART", "C0FFFFFF", 0.55, 0.02, -0.03),
        L("BEAM", "BORDER", "C8D8F4FF", 1.0, 0.3, 0.0),
    ]),
    ("Ember Glow", "Fire: a hot orange glow breathing at the edges, red-orange light sweeps and rising sparks.", [
        L("FLAT", "ART", "20FF6020", 1.0, pulse_amp=0.5, pulse_rate=0.3),
        L("BEAM", "ART", "80FFFFFF", 2.2, 0.16, 0.0, **drift(0.05, 0.05, 0.12, 0.8)),
        L("RAYS", "ART", "48FF9040", 1.8, 0.10, 0.0),
        L("VIGNETTE", "ART", "78FF6020", 0.77, 0.0, 0.0, pulse_amp=0.4, pulse_rate=0.4),
        L("GLITTER", "ART", "B0FFB060", 0.5, 0.01, -0.12),
        L("BEAM", "BORDER", "D0FF8030", 1.0, 0.4, 0.0, **drift(0.07, 0.05, 0.2, 0.9)),
    ]),
    ("Rainbow Prism", "Bright rainbow rays and beams crossing each other with a faint weave and glitter.", [
        L("RAYS", "ART", "70FFFFFF", 1.8, 0.10, 0.0, **rainbow(0.1, 0.55)),
        L("BEAM", "ART", "70FFFFFF", 2.4, -0.12, 0.05, **rainbow(0.14, 0.6)),
        L("WEAVE", "ART", "38FFFFFF", 0.45, 0.0, 0.0),
        L("GLITTER", "ART", "A0FFFFFF", 0.6, 0.02, -0.04),
        L("BEAM", "CARD", "40FFFFFF", 3.0, 0.2, 0.0, **rainbow(0.1, 0.3)),
        L("BEAM", "BORDER", "D0FFFFFF", 1.0, 0.4, 0.0, **rainbow(0.4, 0.9)),
    ]),
    ("Silver Chrome", "Cold silver: a bright sheen sweeping across, a thin streak and fine sparkles.", [
        L("SHEEN", "ART", "A0FFFFFF", 1.4, 0.12, 0.0),
        L("BEAM", "ART", "70FFFFFF", 3.0, -0.10, 0.0),
        L("GLITTER", "ART", "A0FFFFFF", 0.5, 0.02, -0.03),
        L("VIGNETTE", "ART", "40E0E8FF", 0.77, 0.0, 0.0),
        L("BEAM", "BORDER", "D0F0F4FF", 1.0, 0.35, 0.0),
    ]),
    ("Sakura Pastel", "Soft pink and white: a blush wash, drifting petals of light and a pearly frame.", [
        L("FLAT", "CARD", "1CFFB0D0", 1.0, pulse_amp=0.5, pulse_rate=0.2),
        L("CLOUD", "ART", "68FFFFFF", 1.2, 0.03, 0.02, **drift(0.94, 0.05, 0.08, 0.35)),
        L("VDASH", "ART", "34FFFFFF", 0.30, 0.02, 0.0, **drift(0.94, 0.05, 0.08, 0.4)),
        L("GLITTER", "ART", "B0FFFFFF", 0.6, 0.02, -0.05),
        L("BEAM", "BORDER", "C8FFE0F0", 1.0, 0.3, 0.0, **drift(0.94, 0.04, 0.1, 0.3)),
    ]),
    ("Starlight", "A night sky: lots of twinkling stars, a slow blue glow and a deep edge.", [
        L("GLITTER", "ART", "C0FFFFFF", 0.45, 0.01, -0.02),
        L("GLITTER", "ART", "A0C8DCFF", 0.7, -0.01, -0.03),
        L("GLITTER", "CARD", "70FFFFFF", 0.9, 0.0, -0.02),
        L("GLOW", "ART", "50A0B8FF", 1.4, 0.04, 0.02, pulse_amp=0.6, pulse_rate=0.25),
        L("VIGNETTE", "ART", "58405CFF", 0.77, 0.0, 0.0, pulse_amp=0.3, pulse_rate=0.3),
        L("BEAM", "BORDER", "B0B8C8FF", 1.0, 0.3, 0.0),
    ]),
    ("Hologram Stripes", "Cyan and magenta light stripes with vertical dashes in a band, like a hologram sticker.", [
        L("RAYS", "ART", "68FFFFFF", 1.8, 0.10, 0.0, **drift(0.75, 0.2, 0.15, 0.7)),
        L("VDASHBAND", "ART", "98FFFFFF", 1.4, 0.10, 0.0),
        L("BEAM", "ART", "50FFFFFF", 2.4, -0.12, 0.0, **drift(0.5, 0.2, 0.18, 0.7)),
        L("GLITTER", "ART", "80FFFFFF", 0.7, 0.02, -0.04),
        L("BEAM", "BORDER", "D0FFFFFF", 1.0, 0.4, 0.0, **drift(0.62, 0.15, 0.25, 0.8)),
    ]),
]


BG = "#1e1f26"
PANEL = "#272a34"
FG = "#e8e8ee"
MUTED = "#9aa0b4"
ACCENT = "#ffd54a"


def hex_to_rgb_alpha(text):
    text = (text or "FFFFFFFF").lstrip("#")
    if len(text) == 6:
        text = "FF" + text
    try:
        value = int(text, 16)
    except ValueError:
        value = 0xFFFFFFFF
    return (value >> 16) & 255, (value >> 8) & 255, value & 255, (value >> 24) & 255


def rgb_alpha_to_hex(r, g, b, a):
    return "%02X%02X%02X%02X" % (a, r, g, b)


def hsv_hex(h):
    import colorsys
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, 0.8, 1.0)
    return "#%02x%02x%02x" % (int(r * 255), int(g * 255), int(b * 255))


class Slider(ttk.Frame):
    """A label, a slider and the number, with a one line explanation."""

    def __init__(self, parent, label, low, high, step, hint, on_change, decimals=2):
        super().__init__(parent)
        self.low, self.high, self.step, self.decimals = low, high, step, decimals
        self.on_change = on_change
        self.var = tk.DoubleVar(value=low)
        self.columnconfigure(1, weight=1)
        ttk.Label(self, text=label, width=18).grid(row=0, column=0, sticky="w")
        self.scale = ttk.Scale(self, from_=low, to=high, variable=self.var, command=self._moved)
        self.scale.grid(row=0, column=1, sticky="ew", padx=6)
        self.value_label = ttk.Label(self, text="", width=7, anchor="e")
        self.value_label.grid(row=0, column=2)
        ttk.Label(self, text=hint, style="Hint.TLabel").grid(row=1, column=0, columnspan=3, sticky="w")
        self.quiet = False

    def _moved(self, _value):
        value = round(self.var.get() / self.step) * self.step
        self.value_label.config(text=("%." + str(self.decimals) + "f") % value)
        if not self.quiet:
            self.on_change(value)

    def set(self, value):
        self.quiet = True
        self.var.set(value)
        self.value_label.config(text=("%." + str(self.decimals) + "f") % value)
        self.quiet = False

    def enable(self, on):
        self.scale.state(["!disabled"] if on else ["disabled"])


class Studio(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Rarity Studio")
        self.geometry("1540x800")
        self.minsize(1380, 720)
        self.configure(bg=BG)
        self.settings = self.load_settings()
        self.game_folder = tk.StringVar(value=self.settings.get("game_folder", DEFAULT_GAME_FOLDER))
        self.auto_apply = tk.BooleanVar(value=self.settings.get("auto_apply", True))
        self.builtin = {}          # id -> {"name", "layers"} from the game
        self.data = {}             # id -> {"name", "layers"}: changed built-ins and custom rarities
        self.current_id = None
        self.current_layer = None
        self.save_job = None
        self.library_preview = None
        self.loading = False
        self.setup_style()
        self.build_ui()
        self.load_files()
        self.select_rarity("SR")

    # ---------------------------------------------------------------- style
    def setup_style(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        base = ("Segoe UI", 10)
        style.configure(".", background=BG, foreground=FG, font=base, fieldbackground=PANEL)
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=BG, foreground=FG)
        style.configure("Hint.TLabel", background=BG, foreground=MUTED, font=("Segoe UI", 8))
        style.configure("Title.TLabel", background=BG, foreground=ACCENT, font=("Segoe UI", 12, "bold"))
        style.configure("TLabelframe", background=BG, foreground=ACCENT, bordercolor="#3a3f50")
        style.configure("TLabelframe.Label", background=BG, foreground=ACCENT, font=("Segoe UI", 10, "bold"))
        style.configure("TButton", background="#3a3f50", foreground=FG, padding=6, borderwidth=0)
        style.map("TButton", background=[("active", "#4a5068"), ("disabled", "#2a2d38")], foreground=[("disabled", "#666a7a")])
        style.configure("Accent.TButton", background="#c9a227", foreground="#111111", font=("Segoe UI", 10, "bold"))
        style.map("Accent.TButton", background=[("active", "#e0b52e")])
        style.configure("TCheckbutton", background=BG, foreground=FG)
        style.map("TCheckbutton", background=[("active", BG)])
        style.configure("TCombobox", fieldbackground=PANEL, background="#3a3f50", foreground=FG, arrowcolor=FG)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL)], foreground=[("readonly", FG)])
        style.configure("Horizontal.TScale", background=BG, troughcolor="#3a3f50")
        self.option_add("*TCombobox*Listbox.background", PANEL)
        self.option_add("*TCombobox*Listbox.foreground", FG)

    # ------------------------------------------------------------------- ui
    def build_ui(self):
        top = ttk.Frame(self, padding=(12, 10))
        top.pack(fill="x")
        ttk.Label(top, text="Rarity Studio", style="Title.TLabel").pack(side="left")
        ttk.Button(top, text="Open game folder", command=self.open_folder).pack(side="right")
        ttk.Button(top, text="Browse...", command=self.browse).pack(side="right", padx=6)
        entry = ttk.Entry(top, textvariable=self.game_folder, width=42)
        entry.pack(side="right")
        entry.bind("<FocusOut>", lambda _e: self.on_folder_changed())
        ttk.Label(top, text="Game folder:").pack(side="right", padx=(0, 6))

        bar = ttk.Frame(self, padding=(12, 0, 12, 8))
        bar.pack(fill="x")
        ttk.Checkbutton(bar, text="Apply to the game automatically", variable=self.auto_apply,
                        command=self.on_auto_toggle).pack(side="left")
        ttk.Button(bar, text="Apply to game now", style="Accent.TButton", command=self.save_now).pack(side="left", padx=10)
        self.status = ttk.Label(bar, text="", style="Hint.TLabel")
        self.status.pack(side="left", padx=6)
        ttk.Button(bar, text="Publish to friends", style="Accent.TButton", command=self.publish_to_friends).pack(side="right", padx=(0, 18))
        ttk.Button(bar, text="Share pack...", command=self.share_pack).pack(side="right", padx=(0, 6))
        ttk.Button(bar, text="Get pack...", command=self.get_pack).pack(side="right", padx=(0, 6))
        ttk.Button(bar, text="Foil library...", command=self.open_library).pack(side="right", padx=(0, 18))
        ttk.Button(bar, text="Import...", command=self.import_file).pack(side="right")
        ttk.Button(bar, text="Export...", command=self.export_file).pack(side="right", padx=6)

        body = ttk.Frame(self, padding=(12, 0, 12, 12))
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=0)
        body.columnconfigure(1, weight=0)
        body.columnconfigure(2, weight=1)
        body.columnconfigure(3, weight=0)
        body.rowconfigure(0, weight=1)

        # rarities
        left = ttk.LabelFrame(body, text="1  Rarity", padding=8)
        left.grid(row=0, column=0, sticky="ns", padx=(0, 10))
        self.rarity_list = tk.Listbox(left, width=26, height=16, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111",
                                      font=("Segoe UI", 10), activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False)
        self.rarity_list.pack(fill="both", expand=True)
        self.rarity_list.bind("<<ListboxSelect>>", self.on_rarity_pick)
        ttk.Label(left, text="Name shown in the game:").pack(anchor="w", pady=(8, 0))
        self.name_var = tk.StringVar()
        name_entry = ttk.Entry(left, textvariable=self.name_var)
        name_entry.pack(fill="x")
        name_entry.bind("<KeyRelease>", lambda _e: self.on_name_changed())
        self.create_button = ttk.Button(left, text="Create this rarity", style="Accent.TButton", command=self.create_current)
        self.create_button.pack(fill="x", pady=(8, 0))
        self.reset_button = ttk.Button(left, text="Reset to original", command=self.reset_current)
        self.reset_button.pack(fill="x", pady=(6, 0))
        self.copy_button = ttk.Button(left, text="Copy layers from...", command=self.copy_layers_from)
        self.copy_button.pack(fill="x", pady=(6, 0))

        # layers
        mid = ttk.LabelFrame(body, text="2  Layers (drawn in this order)", padding=8)
        mid.grid(row=0, column=1, sticky="ns", padx=(0, 10))
        self.layer_list = tk.Listbox(mid, width=34, height=14, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111",
                                     font=("Segoe UI", 10), activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False)
        self.layer_list.pack(fill="both", expand=True)
        self.layer_list.bind("<<ListboxSelect>>", self.on_layer_pick)
        row = ttk.Frame(mid)
        row.pack(fill="x", pady=(8, 0))
        self.add_button = ttk.Menubutton(row, text="Add layer ▾")
        menu = tk.Menu(self.add_button, tearoff=0, bg=PANEL, fg=FG)
        for name in TEMPLATES:
            menu.add_command(label=name, command=lambda n=name: self.add_layer(n))
        self.add_button["menu"] = menu
        self.add_button.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Copy", width=6, command=self.duplicate_layer).pack(side="left", padx=4)
        ttk.Button(row, text="Delete", width=7, command=self.remove_layer).pack(side="left")
        row2 = ttk.Frame(mid)
        row2.pack(fill="x", pady=(6, 0))
        ttk.Button(row2, text="▲ Up", command=lambda: self.move_layer(-1)).pack(side="left", fill="x", expand=True)
        ttk.Button(row2, text="▼ Down", command=lambda: self.move_layer(1)).pack(side="left", fill="x", expand=True, padx=(6, 0))
        self.enabled_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(mid, text="This layer is on", variable=self.enabled_var, command=self.on_enabled).pack(anchor="w", pady=(8, 0))

        # settings
        right = ttk.LabelFrame(body, text="3  Layer settings", padding=10)
        right.grid(row=0, column=2, sticky="nsew")
        right.columnconfigure(0, weight=1)
        self.settings_frame = right

        look = ttk.Frame(right)
        look.grid(row=0, column=0, sticky="ew")
        look.columnconfigure(1, weight=1)
        look.columnconfigure(3, weight=1)
        ttk.Label(look, text="Effect", width=8).grid(row=0, column=0, sticky="w")
        self.pattern_box = ttk.Combobox(look, state="readonly", values=[label for _, label, _ in PATTERNS])
        self.pattern_box.grid(row=0, column=1, sticky="ew", padx=(0, 14))
        self.pattern_box.bind("<<ComboboxSelected>>", lambda _e: self.on_pattern())
        ttk.Label(look, text="Where", width=7).grid(row=0, column=2, sticky="w")
        self.region_box = ttk.Combobox(look, state="readonly", values=[label for _, label in REGIONS])
        self.region_box.grid(row=0, column=3, sticky="ew")
        self.region_box.bind("<<ComboboxSelected>>", lambda _e: self.on_region())
        self.pattern_hint = ttk.Label(look, text="", style="Hint.TLabel")
        self.pattern_hint.grid(row=1, column=0, columnspan=4, sticky="w", pady=(2, 8))

        color = ttk.LabelFrame(right, text="Color", padding=8)
        color.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        color.columnconfigure(1, weight=1)
        crow = ttk.Frame(color)
        crow.grid(row=0, column=0, columnspan=2, sticky="ew")
        self.swatch = tk.Label(crow, text="   ", width=6, bg="#ffffff", relief="flat")
        self.swatch.pack(side="left")
        ttk.Button(crow, text="Pick color...", command=self.pick_color).pack(side="left", padx=8)
        ttk.Label(crow, text="Color mode", width=11).pack(side="left", padx=(12, 0))
        self.mode_box = ttk.Combobox(crow, state="readonly", width=22, values=["Fixed color", "Rainbow cycle", "Drift around a color"])
        self.mode_box.pack(side="left")
        self.mode_box.bind("<<ComboboxSelected>>", lambda _e: self.on_mode())
        self.alpha = Slider(color, "Strength", 0, 255, 1, "How strongly the effect shows (0 = invisible, 255 = full).", lambda v: self.set_alpha(v), 0)
        self.alpha.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(6, 0))
        self.sat = Slider(color, "Color richness", 0, 1, 0.01, "Rainbow and drifting colors only: pale to vivid.", lambda v: self.set_field("sat", v))
        self.sat.grid(row=2, column=0, columnspan=2, sticky="ew")
        self.hue_rate = Slider(color, "Color speed", -2, 2, 0.01, "How fast the colors change (negative = the other way).", lambda v: self.set_field("hue_rate", v))
        self.hue_rate.grid(row=3, column=0, columnspan=2, sticky="ew")
        self.hue_center = Slider(color, "Center color", 0, 1, 0.01, "Drift mode: the color it moves around.", lambda v: self.set_field("hue_center", v))
        self.hue_center.grid(row=4, column=0, columnspan=2, sticky="ew")
        self.hue_strip = tk.Canvas(color, height=10, bg=BG, highlightthickness=0)
        self.hue_strip.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(0, 2))
        self.hue_strip.bind("<Configure>", lambda _e: self.draw_hue_strip())
        self.hue_amp = Slider(color, "Drift amount", 0, 0.5, 0.01, "Drift mode: how far the color moves from the center.", lambda v: self.set_field("hue_amp", v))
        self.hue_amp.grid(row=6, column=0, columnspan=2, sticky="ew")

        move = ttk.LabelFrame(right, text="Size and movement", padding=8)
        move.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        move.columnconfigure(0, weight=1)
        self.tile = Slider(move, "Pattern size", 0.1, 4, 0.05, "Bigger = a larger pattern (in card widths).", lambda v: self.set_field("tile", v))
        self.tile.grid(row=0, column=0, sticky="ew")
        self.vx = Slider(move, "Move sideways", -1, 1, 0.01, "Speed to the right (negative = left). 0 = stays.", lambda v: self.set_field("vx", v))
        self.vx.grid(row=1, column=0, sticky="ew")
        self.vy = Slider(move, "Move up / down", -1, 1, 0.01, "Speed downwards (negative = up). 0 = stays.", lambda v: self.set_field("vy", v))
        self.vy.grid(row=2, column=0, sticky="ew")

        pulse = ttk.LabelFrame(right, text="Pulse (fade in and out)", padding=8)
        pulse.grid(row=3, column=0, sticky="ew")
        pulse.columnconfigure(0, weight=1)
        self.pulse_amp = Slider(pulse, "Pulse strength", 0, 1, 0.01, "0 = steady. 1 = fades all the way in and out.", lambda v: self.set_field("pulse_amp", v))
        self.pulse_amp.grid(row=0, column=0, sticky="ew")
        self.pulse_rate = Slider(pulse, "Pulse speed", 0, 3, 0.01, "How many times per second it fades.", lambda v: self.set_field("pulse_rate", v))
        self.pulse_rate.grid(row=1, column=0, sticky="ew")
        self.draw_hue_strip()
        self.build_preview(body)

    # -------------------------------------------------------------- preview
    def build_preview(self, parent):
        box = ttk.LabelFrame(parent, text="Preview", padding=8)
        box.grid(row=0, column=3, sticky="ns", padx=(10, 0))
        self.preview_ok = rarity_preview is not None
        if not self.preview_ok:
            ttk.Label(box, text="The preview needs Pillow and numpy.\nInstall them with:\n\n    pip install pillow numpy\n\nand start the Studio again.\n\n(%s)" % PREVIEW_ERROR,
                      wraplength=260, justify="left").pack()
            return
        self.pw, self.ph = 280, 400
        self.canvas = tk.Canvas(box, width=self.pw, height=self.ph, bg="#101218", highlightthickness=0)
        self.canvas.pack()
        self.canvas_image = self.canvas.create_image(0, 0, anchor="nw")
        self.frame_var = tk.StringVar(value="Effect monster")
        self.solo_var = tk.BooleanVar(value=False)
        self.animate_var = tk.BooleanVar(value=True)
        self.art_image = None
        self.base_key = None
        self.base_array = None
        self.photo = None
        row = ttk.Frame(box)
        row.pack(fill="x", pady=(8, 0))
        ttk.Label(row, text="Card type").pack(side="left")
        frames = ttk.Combobox(row, state="readonly", textvariable=self.frame_var, values=list(rarity_preview.FRAMES), width=16)
        frames.pack(side="left", padx=6)
        row2 = ttk.Frame(box)
        row2.pack(fill="x", pady=(6, 0))
        ttk.Button(row2, text="Use my picture...", command=self.pick_picture).pack(side="left", fill="x", expand=True)
        ttk.Button(row2, text="Default", width=8, command=self.default_picture).pack(side="left", padx=(6, 0))
        ttk.Checkbutton(box, text="Show only the selected layer", variable=self.solo_var).pack(anchor="w", pady=(8, 0))
        ttk.Checkbutton(box, text="Animate", variable=self.animate_var).pack(anchor="w")
        ttk.Label(box, text="Close to the game's look, but the game\nis the final judge.", style="Hint.TLabel", justify="left").pack(anchor="w", pady=(8, 0))
        self.t0 = time.time()
        self.after(80, self.tick)

    def pick_picture(self):
        filename = filedialog.askopenfilename(title="Pick a card picture", filetypes=[("Pictures", "*.jpg *.jpeg *.png *.bmp"), ("All files", "*.*")])
        if filename:
            try:
                self.art_image = Image.open(filename)
                self.art_image.load()
                self.base_key = None
            except Exception as e:
                messagebox.showerror("Rarity Studio", str(e))

    def default_picture(self):
        self.art_image = None
        self.base_key = None

    def tick(self):
        try:
            if self.animate_var.get() or self.photo is None:
                self.render_preview()
        except Exception as e:
            self.set_status("Preview problem: %s" % e)
        finally:
            self.after(60, self.tick)

    def render_preview(self):
        key = (self.frame_var.get(), id(self.art_image))
        if key != self.base_key:
            self.base_array = rarity_preview.make_card(self.pw, self.ph, self.frame_var.get(), self.art_image)
            self.base_key = key
        layers = self.layers()
        if self.solo_var.get() and self.layer() is not None:
            layers = [self.layer()]
        if getattr(self, "library_preview", None) is not None:
            layers = self.library_preview
        seconds = (time.time() - self.t0) * rarity_preview.SPEED
        frame = rarity_preview.compose(self.base_array, layers, seconds)
        self.photo = ImageTk.PhotoImage(rarity_preview.to_image(frame))
        self.canvas.itemconfig(self.canvas_image, image=self.photo)

    # ------------------------------------------------------ publish to friends
    def github_token(self):
        """The GitHub login the git credential manager of this PC already has (nothing is stored by the Studio)."""
        import subprocess
        try:
            out = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n", capture_output=True, text=True, timeout=30).stdout
            for line in out.splitlines():
                if line.startswith("password="):
                    return line[len("password="):]
        except Exception:
            pass
        from tkinter import simpledialog
        return simpledialog.askstring("GitHub", "Paste a GitHub token that may publish releases of the mod repository\n(it is only used now, not saved):", show="*", parent=self)

    def publish_to_friends(self):
        """Uploads your rarities as one file to the mod's GitHub release 'rarity-pack'. Everyone with the game gets a button to install it."""
        import urllib.request
        import urllib.error
        folder = self.game_folder.get()
        if not messagebox.askyesno("Rarity Studio", "Publish your rarities?\n\nEveryone who uses the mod gets a button on the main menu to install your rarity designs, the rarity of your cards and your deck rarities (their own files are kept as .backup)."):
            return
        self.save_now()
        pack = {"version": 1, "decks": {}}
        try:
            with open(self.path("custom_rarities.json"), "r", encoding="utf-8") as f:
                pack["custom_rarities"] = json.load(f)
        except FileNotFoundError:
            pass
        except Exception as e:
            messagebox.showerror("Rarity Studio", "custom_rarities.json could not be read:\n%s" % e)
            return
        if os.path.exists(self.path("rarities.txt")):
            with open(self.path("rarities.txt"), "r", encoding="utf-8") as f:
                pack["rarities_txt"] = f.read()
        deck_folder = self.path("deck")
        if os.path.isdir(deck_folder):
            for entry in os.listdir(deck_folder):
                if entry.endswith(".rarity"):
                    with open(os.path.join(deck_folder, entry), "r", encoding="utf-8") as f:
                        pack["decks"][entry[:-len(".rarity")]] = f.read()
        data = json.dumps(pack).encode("utf-8")
        if len(data) > 3 * 1024 * 1024:
            messagebox.showerror("Rarity Studio", "The pack is too big (%d KB)." % (len(data) // 1024))
            return
        token = self.github_token()
        if not token:
            self.set_status("Publishing cancelled: no GitHub login found.")
            return
        repo = "sushifishblock/EDOproextend"
        headers = {"Authorization": "token " + token, "User-Agent": "rarity-studio", "Accept": "application/vnd.github+json"}

        def call(method, url, body=None, content_type="application/json"):
            request = urllib.request.Request(url, data=body, method=method, headers=dict(headers, **({"Content-Type": content_type} if body is not None else {})))
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    text = response.read().decode("utf-8")
                    return response.status, (json.loads(text) if text else {})
            except urllib.error.HTTPError as e:
                return e.code, {}

        try:
            self.set_status("Publishing...")
            self.update_idletasks()
            status, release = call("GET", "https://api.github.com/repos/%s/releases/tags/rarity-pack" % repo)
            if status == 404:
                body = json.dumps({"tag_name": "rarity-pack", "target_commitish": "main", "name": "Rarity pack",
                                   "body": "The author's rarities. The game offers them with a button on the main menu. Not a game version.", "prerelease": True}).encode()
                status, release = call("POST", "https://api.github.com/repos/%s/releases" % repo, body)
            if status not in (200, 201) or "id" not in release:
                raise RuntimeError("could not reach the release (HTTP %s); is the GitHub login right?" % status)
            for asset in release.get("assets", []):
                if asset.get("name") == "rarity_pack.json":
                    call("DELETE", "https://api.github.com/repos/%s/releases/assets/%s" % (repo, asset["id"]))
            status, asset = call("POST", "https://uploads.github.com/repos/%s/releases/%s/assets?name=rarity_pack.json" % (repo, release["id"]), data)
            if status not in (200, 201) or "id" not in asset:
                raise RuntimeError("the upload failed (HTTP %s)" % status)
            # this game folder already has these rarities: no button for you
            with open(self.path("rarity_pack.id"), "w", encoding="utf-8") as f:
                f.write(str(asset["id"]))
            self.set_status("Published at %s. Your friends see the button the next time they open the game." % time.strftime("%H:%M:%S"))
            messagebox.showinfo("Rarity Studio", "Published.\n\nYour friends will see \"New rarities from the author - click to install\" on the main menu of their game (version 1.5 or newer).")
        except Exception as e:
            self.set_status("Publishing failed: %s" % e)
            messagebox.showerror("Rarity Studio", "Publishing failed:\n%s" % e)

    # ---------------------------------------------------------- share packs
    def share_pack(self):
        """One zip with everything a friend needs to see your rarities: the rarity designs, which card has which rarity and the deck rarities."""
        folder = self.game_folder.get()
        self.save_now()
        filename = filedialog.asksaveasfilename(title="Save your rarity pack", defaultextension=".zip", initialfile="my_rarity_pack.zip", filetypes=[("Rarity pack", "*.zip")])
        if not filename:
            return
        try:
            count = 0
            with zipfile.ZipFile(filename, "w", zipfile.ZIP_DEFLATED) as z:
                for name in ("custom_rarities.json", "rarities.txt"):
                    full = os.path.join(folder, name)
                    if os.path.exists(full):
                        z.write(full, name)
                        count += 1
                deck_folder = os.path.join(folder, "deck")
                if os.path.isdir(deck_folder):
                    for entry in os.listdir(deck_folder):
                        if entry.endswith(".rarity"):
                            z.write(os.path.join(deck_folder, entry), "deck/" + entry)
                            count += 1
            self.set_status("Saved the pack (%d files). Send it to your friend: they press \"Get pack...\"." % count)
        except Exception as e:
            messagebox.showerror("Rarity Studio", str(e))

    def get_pack(self):
        """Install a pack from a friend: it replaces your rarity designs and card rarities (your old files are kept as .backup)."""
        filename = filedialog.askopenfilename(title="Open a rarity pack", filetypes=[("Rarity pack", "*.zip")])
        if not filename:
            return
        if not messagebox.askyesno("Rarity Studio", "This replaces your rarity designs, the rarity of your cards and the deck rarities with the ones in the pack.\nYour current files are kept as .backup files. Continue?"):
            return
        folder = self.game_folder.get()
        try:
            with zipfile.ZipFile(filename) as z:
                for member in z.namelist():
                    name = member.replace("\\", "/")
                    allowed = name in ("custom_rarities.json", "rarities.txt") or (name.startswith("deck/") and name.endswith(".rarity") and "/" not in name[5:] and ".." not in name)
                    if not allowed:
                        continue
                    target = os.path.join(folder, *name.split("/"))
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    if os.path.exists(target):
                        shutil.copyfile(target, target + ".backup")
                    with z.open(member) as src, open(target, "wb") as dst:
                        shutil.copyfileobj(src, dst)
            self.load_files()
            self.select_rarity(self.current_id or "SR")
            self.set_status("Pack installed. Restart the game so the card rarities are read again (the designs update by themselves).")
        except Exception as e:
            messagebox.showerror("Rarity Studio", str(e))

    # -------------------------------------------------------------- library
    def open_library(self):
        """Ready-made foils: look at them in the preview, add them as a new custom rarity or as layers of the selected one."""
        if getattr(self, "library_window", None) is not None and self.library_window.winfo_exists():
            self.library_window.lift()
            return
        win = tk.Toplevel(self)
        self.library_window = win
        win.title("Foil library")
        win.configure(bg=BG)
        win.geometry("520x470")
        win.transient(self)
        ttk.Label(win, text="Ready-made foils", style="Title.TLabel", padding=(12, 10, 12, 0)).pack(anchor="w")
        ttk.Label(win, text="Click one to see it in the preview. Nothing is added until you press a button below.", style="Hint.TLabel", padding=(12, 0)).pack(anchor="w")
        listbox = tk.Listbox(win, bg=PANEL, fg=FG, selectbackground="#c9a227", selectforeground="#111111", font=("Segoe UI", 11),
                             activestyle="none", borderwidth=0, highlightthickness=0, exportselection=False, height=12)
        listbox.pack(fill="both", expand=True, padx=12, pady=8)
        for name, _desc, _layers in PRESETS:
            listbox.insert("end", name)
        info = ttk.Label(win, text="", wraplength=480, justify="left", padding=(12, 0))
        info.pack(anchor="w")

        def current():
            sel = listbox.curselection()
            return PRESETS[sel[0]] if sel else None

        def show(_e=None):
            preset = current()
            if preset:
                self.library_preview = [dict(layer) for layer in preset[2]]
                info.config(text=preset[1])

        def close():
            self.library_preview = None
            win.destroy()

        def add_new():
            preset = current()
            if not preset:
                return
            free = [rid for rid in CUSTOM_IDS if rid not in self.data]
            if not free:
                messagebox.showinfo("Rarity Studio", "All 8 custom slots are used. Delete one first.")
                return
            rid = free[0]
            self.data[rid] = {"name": preset[0], "layers": copy.deepcopy(preset[2])}
            self.changed()
            self.select_rarity(rid)
            self.set_status("Added \"%s\" as %s. Pick it for a card in the game." % (preset[0], self.label_for(rid).split("   ")[0]))

        def add_layers():
            preset = current()
            rid = self.current_id
            if not preset or rid is None:
                return
            if rid not in self.data:
                if rid in CUSTOM_IDS:
                    self.create_current()
                    self.data[rid]["layers"] = []
                elif not self.ensure_editable():
                    return
            self.data[rid]["layers"].extend(copy.deepcopy(preset[2]))
            self.changed()
            self.select_rarity(rid)

        listbox.bind("<<ListboxSelect>>", show)
        row = ttk.Frame(win, padding=12)
        row.pack(fill="x")
        ttk.Button(row, text="Add as a new custom rarity", style="Accent.TButton", command=add_new).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Add its layers to the selected rarity", command=add_layers).pack(side="left", fill="x", expand=True, padx=(8, 0))
        win.protocol("WM_DELETE_WINDOW", close)
        listbox.selection_set(0)
        show()

    # ------------------------------------------------------------ settings
    def load_settings(self):
        try:
            with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}

    def store_settings(self):
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump({"game_folder": self.game_folder.get(), "auto_apply": self.auto_apply.get()}, f)
        except Exception:
            pass

    def on_auto_toggle(self):
        self.store_settings()
        if self.auto_apply.get():
            self.save_now()

    def browse(self):
        folder = filedialog.askdirectory(initialdir=self.game_folder.get() or DEFAULT_GAME_FOLDER, title="Pick the folder with EDOPro.exe")
        if folder:
            self.game_folder.set(os.path.normpath(folder))
            self.on_folder_changed()

    def open_folder(self):
        try:
            os.startfile(self.game_folder.get())
        except Exception as e:
            messagebox.showerror("Rarity Studio", str(e))

    def on_folder_changed(self):
        self.store_settings()
        self.load_files()
        self.select_rarity(self.current_id or "SR")

    # --------------------------------------------------------------- files
    def path(self, name):
        return os.path.join(self.game_folder.get(), name)

    def load_files(self):
        self.builtin = {}
        self.data = {}
        try:
            with open(self.path("rarities_builtin.json"), "r", encoding="utf-8") as f:
                for entry in json.load(f).get("rarities", []):
                    self.builtin[entry["id"]] = {"name": entry.get("name", ""), "layers": [self.fix_layer(l) for l in entry.get("layers", [])]}
        except Exception:
            self.set_status("The game has not written rarities_builtin.json yet: start the game once (the new version) and pick the folder again. You can still make custom rarities.")
        self.load_custom(self.path("custom_rarities.json"), replace=True)

    def load_custom(self, filename, replace=False):
        try:
            with open(filename, "r", encoding="utf-8") as f:
                root = json.load(f)
        except FileNotFoundError:
            return False
        except Exception as e:
            messagebox.showerror("Rarity Studio", "Could not read the file:\n%s" % e)
            return False
        if replace:
            self.data = {}
        for entry in root.get("rarities", []):
            rid = entry.get("id")
            if rid in ALL_IDS:
                self.data[rid] = {"name": entry.get("name", ""), "layers": [self.fix_layer(l) for l in entry.get("layers", [])]}
        return True

    @staticmethod
    def fix_layer(layer):
        fixed = dict(NEW_LAYER)
        fixed.update(layer)
        if fixed["pattern"] not in PATTERN_LABEL:
            fixed["pattern"] = "FLAT"
        if fixed["region"] not in REGION_LABEL:
            fixed["region"] = "CARD"
        return fixed

    def document(self):
        rarities = []
        for rid in ALL_IDS:
            if rid in self.data:
                entry = self.data[rid]
                rarities.append({"id": rid, "name": entry["name"] or BUILTIN_NAMES.get(rid, ""), "layers": entry["layers"]})
        return {"version": 1, "rarities": rarities}

    def write_file(self, filename):
        text = json.dumps(self.document(), indent=1)
        tmp = filename + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, filename)

    def save_now(self):
        if self.save_job:
            self.after_cancel(self.save_job)
            self.save_job = None
        folder = self.game_folder.get()
        if not os.path.isdir(folder):
            self.set_status("The game folder does not exist.")
            return
        try:
            self.write_file(self.path("custom_rarities.json"))
            self.set_status("Saved to the game at %s - the game picks it up within a second." % time.strftime("%H:%M:%S"))
        except Exception as e:
            self.set_status("Could not save: %s" % e)

    def changed(self):
        if self.loading:
            return
        self.refresh_rarity_list(keep=True)
        if self.auto_apply.get():
            if self.save_job:
                self.after_cancel(self.save_job)
            self.save_job = self.after(350, self.save_now)
        else:
            self.set_status("Changed - press \"Apply to game now\".")

    def import_file(self):
        filename = filedialog.askopenfilename(title="Import rarities", filetypes=[("Rarity file", "*.json")])
        if filename and self.load_custom(filename):
            self.changed()
            self.select_rarity(self.current_id or "SR")

    def export_file(self):
        filename = filedialog.asksaveasfilename(title="Export rarities", defaultextension=".json", initialfile="my_rarities.json", filetypes=[("Rarity file", "*.json")])
        if filename:
            try:
                self.write_file(filename)
                self.set_status("Exported to %s" % filename)
            except Exception as e:
                messagebox.showerror("Rarity Studio", str(e))

    def set_status(self, text):
        self.status.config(text=text)

    # ------------------------------------------------------------- rarities
    def label_for(self, rid):
        if rid in BUILTIN_IDS:
            name = (self.data.get(rid, {}).get("name") or BUILTIN_NAMES[rid])
            return "%s%s" % (name, "   * changed" if rid in self.data else "")
        if rid in self.data:
            return "%s   (custom)" % (self.data[rid]["name"] or "Custom %s" % rid[1:])
        return "Custom %s   (empty slot)" % rid[1:]

    def refresh_rarity_list(self, keep=False):
        selected = self.current_id
        self.rarity_list.delete(0, "end")
        for rid in ALL_IDS:
            self.rarity_list.insert("end", self.label_for(rid))
        if selected in ALL_IDS:
            self.rarity_list.selection_set(ALL_IDS.index(selected))

    def on_rarity_pick(self, _event):
        sel = self.rarity_list.curselection()
        if sel:
            self.select_rarity(ALL_IDS[sel[0]])

    def select_rarity(self, rid):
        self.current_id = rid
        self.current_layer = None
        self.refresh_rarity_list()
        defined = rid in self.data or rid in BUILTIN_IDS
        self.loading = True
        self.name_var.set((self.data.get(rid, {}).get("name") or BUILTIN_NAMES.get(rid, "")) if defined else "")
        self.loading = False
        self.create_button.state(["!disabled"] if (rid in CUSTOM_IDS and rid not in self.data) else ["disabled"])
        if rid in BUILTIN_IDS:
            self.reset_button.config(text="Reset to original")
            self.reset_button.state(["!disabled"] if rid in self.data else ["disabled"])
        else:
            self.reset_button.config(text="Delete this rarity")
            self.reset_button.state(["!disabled"] if rid in self.data else ["disabled"])
        self.copy_button.state(["!disabled"] if defined else ["disabled"])
        self.refresh_layer_list()
        self.show_layer(None)

    def ensure_editable(self):
        """A built-in rarity is copied into the file the first time it is changed."""
        rid = self.current_id
        if rid in self.data:
            return True
        if rid in BUILTIN_IDS:
            base = self.builtin.get(rid)
            if base is None:
                messagebox.showinfo("Rarity Studio", "Start the game once so it can write rarities_builtin.json, then pick the folder again.")
                return False
            self.data[rid] = copy.deepcopy(base)
            self.data[rid]["name"] = base["name"] or BUILTIN_NAMES[rid]
            self.select_rarity_keep_layer()
            return True
        return False

    def select_rarity_keep_layer(self):
        keep = self.current_layer
        self.refresh_rarity_list()
        self.reset_button.state(["!disabled"])
        self.current_layer = keep

    def layers(self):
        rid = self.current_id
        if rid in self.data:
            return self.data[rid]["layers"]
        if rid in BUILTIN_IDS and rid in self.builtin:
            return self.builtin[rid]["layers"]
        return []

    def on_name_changed(self):
        if self.loading or self.current_id not in self.data:
            return
        self.data[self.current_id]["name"] = self.name_var.get()
        self.changed()

    def create_current(self):
        rid = self.current_id
        if rid in CUSTOM_IDS and rid not in self.data:
            self.data[rid] = {"name": "My rarity %s" % rid[1:], "layers": [dict(TEMPLATES["Light sweep"])]}
            self.changed()
            self.select_rarity(rid)

    def reset_current(self):
        rid = self.current_id
        if rid not in self.data:
            return
        what = "Go back to the original look of this rarity?" if rid in BUILTIN_IDS else "Delete this custom rarity? Cards that use it show as normal."
        if messagebox.askyesno("Rarity Studio", what):
            del self.data[rid]
            self.changed()
            self.select_rarity(rid)

    def copy_layers_from(self):
        rid = self.current_id
        win = tk.Toplevel(self)
        win.title("Copy layers from")
        win.configure(bg=BG)
        win.transient(self)
        ttk.Label(win, text="Copy all layers from:", padding=10).pack()
        names = []
        for other in ALL_IDS:
            if other == rid:
                continue
            if other in self.data or (other in BUILTIN_IDS and other in self.builtin):
                names.append(other)
        box = ttk.Combobox(win, state="readonly", values=[self.label_for(n).split("   ")[0] for n in names], width=34)
        box.pack(padx=12)
        if names:
            box.current(0)

        def go():
            if not names or box.current() < 0:
                return
            source = names[box.current()]
            layers = copy.deepcopy(self.data[source]["layers"] if source in self.data else self.builtin[source]["layers"])
            if rid not in self.data:
                if rid in BUILTIN_IDS and not self.ensure_editable():
                    return
                if rid in CUSTOM_IDS:
                    self.data[rid] = {"name": "My rarity %s" % rid[1:], "layers": []}
            self.data[rid]["layers"] = layers
            win.destroy()
            self.changed()
            self.select_rarity(rid)
        ttk.Button(win, text="Copy", style="Accent.TButton", command=go).pack(pady=10)

    # --------------------------------------------------------------- layers
    def refresh_layer_list(self):
        self.layer_list.delete(0, "end")
        for i, layer in enumerate(self.layers()):
            off = "" if layer.get("enabled", True) else "  (off)"
            self.layer_list.insert("end", "%d. %s - %s%s" % (i + 1, PATTERN_LABEL.get(layer["pattern"], layer["pattern"]), REGION_LABEL.get(layer["region"], ""), off))
        if self.current_layer is not None and self.current_layer < self.layer_list.size():
            self.layer_list.selection_set(self.current_layer)

    def on_layer_pick(self, _event):
        sel = self.layer_list.curselection()
        if sel:
            self.show_layer(sel[0])

    def layer(self):
        layers = self.layers()
        if self.current_layer is None or self.current_layer >= len(layers):
            return None
        return layers[self.current_layer]

    def show_layer(self, index):
        self.current_layer = index
        layer = self.layer()
        self.loading = True
        enabled = layer is not None
        for slider in (self.alpha, self.sat, self.hue_rate, self.hue_center, self.hue_amp, self.tile, self.vx, self.vy, self.pulse_amp, self.pulse_rate):
            slider.enable(enabled)
        for widget in (self.pattern_box, self.region_box, self.mode_box):
            widget.state(["!disabled"] if enabled else ["disabled"])
        if layer:
            self.pattern_box.set(PATTERN_LABEL[layer["pattern"]])
            self.pattern_hint.config(text=PATTERN_HINT.get(layer["pattern"], ""))
            self.region_box.set(REGION_LABEL[layer["region"]])
            r, g, b, a = hex_to_rgb_alpha(layer["color"])
            self.swatch.config(bg="#%02x%02x%02x" % (r, g, b))
            self.alpha.set(a)
            self.sat.set(layer["sat"])
            self.hue_rate.set(layer["hue_rate"])
            self.hue_center.set(max(0.0, layer["hue_center"]))
            self.hue_amp.set(layer["hue_amp"])
            self.tile.set(layer["tile"])
            self.vx.set(layer["vx"])
            self.vy.set(layer["vy"])
            self.pulse_amp.set(layer["pulse_amp"])
            self.pulse_rate.set(layer["pulse_rate"])
            self.enabled_var.set(layer.get("enabled", True))
            self.mode_box.current(1 if layer["rainbow"] else (2 if layer["hue_center"] >= 0 else 0))
        else:
            self.pattern_box.set("")
            self.region_box.set("")
            self.mode_box.set("")
            self.pattern_hint.config(text="Pick a layer on the left, or add one.")
        self.loading = False
        self.update_mode_visibility()

    def update_mode_visibility(self):
        mode = self.mode_box.current()
        rainbow, drift = mode == 1, mode == 2
        self.sat.enable(rainbow or drift)
        self.hue_rate.enable(rainbow or drift)
        self.hue_center.enable(drift)
        self.hue_amp.enable(drift)

    def edit(self):
        """Called before a change: makes sure there is something to edit."""
        if self.layer() is None:
            return None
        if self.current_id not in self.data and not self.ensure_editable():
            return None
        return self.layer()

    def set_field(self, key, value):
        if self.loading:
            return
        layer = self.edit()
        if layer is None:
            return
        layer[key] = float(value)
        if key == "hue_center" and self.mode_box.current() == 2:
            layer["hue_center"] = float(value)
        self.changed()

    def set_alpha(self, value):
        if self.loading:
            return
        layer = self.edit()
        if layer is None:
            return
        r, g, b, _ = hex_to_rgb_alpha(layer["color"])
        layer["color"] = rgb_alpha_to_hex(r, g, b, int(value))
        self.changed()

    def pick_color(self):
        layer = self.edit()
        if layer is None:
            return
        r, g, b, a = hex_to_rgb_alpha(layer["color"])
        chosen = colorchooser.askcolor(color="#%02x%02x%02x" % (r, g, b), title="Pick the color")
        if chosen and chosen[0]:
            r, g, b = [int(v) for v in chosen[0]]
            layer["color"] = rgb_alpha_to_hex(r, g, b, a)
            self.swatch.config(bg="#%02x%02x%02x" % (r, g, b))
            self.changed()

    def on_pattern(self):
        layer = self.edit()
        if layer is None:
            return
        for key, label, hint in PATTERNS:
            if label == self.pattern_box.get():
                layer["pattern"] = key
                self.pattern_hint.config(text=hint)
        self.refresh_layer_list()
        self.changed()

    def on_region(self):
        layer = self.edit()
        if layer is None:
            return
        for key, label in REGIONS:
            if label == self.region_box.get():
                layer["region"] = key
        self.refresh_layer_list()
        self.changed()

    def on_mode(self):
        layer = self.edit()
        if layer is None:
            return
        mode = self.mode_box.current()
        layer["rainbow"] = mode == 1
        if mode == 2:
            if layer["hue_center"] < 0:
                layer["hue_center"] = 0.1
            if layer["hue_amp"] <= 0:
                layer["hue_amp"] = 0.1
            if layer["hue_rate"] == 0:
                layer["hue_rate"] = 0.15
        else:
            layer["hue_center"] = -1.0
        if mode == 1 and layer["hue_rate"] == 0:
            layer["hue_rate"] = 0.15
        self.loading = True
        self.hue_center.set(max(0.0, layer["hue_center"]))
        self.hue_amp.set(layer["hue_amp"])
        self.hue_rate.set(layer["hue_rate"])
        self.loading = False
        self.update_mode_visibility()
        self.changed()

    def on_enabled(self):
        layer = self.edit()
        if layer is None:
            return
        layer["enabled"] = self.enabled_var.get()
        self.refresh_layer_list()
        self.changed()

    def add_layer(self, template):
        rid = self.current_id
        if rid not in self.data:
            if rid in CUSTOM_IDS:
                self.create_current()
            elif not self.ensure_editable():
                return
        self.data[rid]["layers"].append(dict(TEMPLATES[template]))
        self.current_layer = len(self.data[rid]["layers"]) - 1
        self.refresh_layer_list()
        self.show_layer(self.current_layer)
        self.changed()

    def duplicate_layer(self):
        layer = self.edit()
        if layer is None:
            return
        layers = self.data[self.current_id]["layers"]
        layers.insert(self.current_layer + 1, dict(layer))
        self.current_layer += 1
        self.refresh_layer_list()
        self.changed()

    def remove_layer(self):
        layer = self.edit()
        if layer is None:
            return
        layers = self.data[self.current_id]["layers"]
        del layers[self.current_layer]
        self.current_layer = min(self.current_layer, len(layers) - 1) if layers else None
        self.refresh_layer_list()
        self.show_layer(self.current_layer)
        self.changed()

    def move_layer(self, delta):
        layer = self.edit()
        if layer is None:
            return
        layers = self.data[self.current_id]["layers"]
        new = self.current_layer + delta
        if 0 <= new < len(layers):
            layers[self.current_layer], layers[new] = layers[new], layers[self.current_layer]
            self.current_layer = new
            self.refresh_layer_list()
            self.changed()

    def draw_hue_strip(self):
        canvas = self.hue_strip
        canvas.delete("all")
        width = max(canvas.winfo_width(), 100)
        steps = 60
        for i in range(steps):
            canvas.create_rectangle(i * width / steps, 0, (i + 1) * width / steps + 1, 10, fill=hsv_hex(i / steps), outline="")


if __name__ == "__main__":
    Studio().mainloop()

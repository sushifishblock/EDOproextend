"""
Sleeves and playmat for the Rarity Studio: pick any picture, the Studio crops / fits it to the right size and writes it into
the game folder (custom_art/). The playmat shows up under the zones of the duel field within a second; sleeves are read when the game starts.
"""
import base64
import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageTk

PANEL = "#272a34"
BG = "#1b1d25"
FG = "#e8e8ee"

SLOTS = {
    "sleeve": {"title": "My sleeve", "file": "sleeve.png", "size": (480, 700), "border": True,
               "help": "The back of your cards (in your hand, deck, face-down on the field). Read when the game starts."},
    "sleeve_opponent": {"title": "Opponent's sleeve", "file": "sleeve_opponent.png", "size": (480, 700), "border": True,
                        "help": "The back of the cards of the player you play against (only on your own screen). Read when the game starts."},
    "playmat": {"title": "Playmat", "file": "playmat.png", "size": (1024, 820), "border": False,
                "help": "Drawn under the zones of the duel field. It updates while the game is running."},
}
FIT_LABELS = {"fill": "Fill the whole area (crops the edges)", "fit": "Fit the whole picture (blurred background)"}
DEFAULTS = {"src": "", "fit": "fill", "zoom": 1.0, "ox": 0.0, "oy": 0.0, "bright": 1.0, "border": 0.02, "live": True}


def render_art(src, size, zoom=1.0, ox=0.0, oy=0.0, bright=1.0, fit="fill", border=0.0):
    """The picture cut to `size`. Everything is relative to the size, so a small preview looks like the real result."""
    out_w, out_h = size
    src = src.convert("RGB")
    sw, sh = src.size
    fill_scale = max(out_w / sw, out_h / sh)
    scale = (fill_scale if fit == "fill" else min(out_w / sw, out_h / sh)) * zoom
    nw, nh = max(1, int(round(sw * scale))), max(1, int(round(sh * scale)))
    big = src.resize((nw, nh), Image.LANCZOS)
    if fit == "fit":
        bw, bh = max(1, int(round(sw * fill_scale))), max(1, int(round(sh * fill_scale)))
        back = src.resize((bw, bh), Image.BILINEAR)
        left, top = (bw - out_w) // 2, (bh - out_h) // 2
        canvas = back.crop((left, top, left + out_w, top + out_h)).filter(ImageFilter.GaussianBlur(max(2, out_w // 30)))
        canvas = ImageEnhance.Brightness(canvas).enhance(0.55)
    else:
        canvas = Image.new("RGB", (out_w, out_h), (0, 0, 0))
    x = (out_w - nw) / 2 + ox * abs(nw - out_w) / 2
    y = (out_h - nh) / 2 + oy * abs(nh - out_h) / 2
    canvas.paste(big, (int(round(x)), int(round(y))))
    if abs(bright - 1.0) > 0.01:
        canvas = ImageEnhance.Brightness(canvas).enhance(bright)
    if border > 0.0:
        width = max(1, int(round(out_w * border)))
        draw = ImageDraw.Draw(canvas)
        for i in range(width):
            draw.rectangle([i, i, out_w - 1 - i, out_h - 1 - i], outline=(0, 0, 0))
    return canvas


def atomic_save_png(image, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    tmp = target + ".tmp"
    image.save(tmp, "PNG")
    os.replace(tmp, target)


def collect_art(folder):
    """The pictures of custom_art/ as base64 text (for a rarity pack)."""
    art = {}
    for slot in SLOTS.values():
        path = os.path.join(folder, "custom_art", slot["file"])
        if os.path.exists(path):
            with open(path, "rb") as f:
                art[slot["file"]] = base64.b64encode(f.read()).decode("ascii")
    return art


class ArtPanel(ttk.Frame):
    def __init__(self, parent, window, slot_key, Slider):
        super().__init__(parent, padding=10)
        self.window = window
        self.studio = window.studio
        self.key = slot_key
        self.slot = SLOTS[slot_key]
        self.src = None
        self.src_small = None
        self.photo = None
        self.job = None
        self.loading = True
        saved = dict(DEFAULTS)
        saved.update(self.studio.settings.get("art", {}).get(slot_key, {}))
        self.cfg = saved
        out_w, out_h = self.slot["size"]
        self.pw = 250 if out_h > out_w else 420
        self.ph = int(round(self.pw * out_h / out_w))

        self.canvas = tk.Canvas(self, width=self.pw, height=self.ph, bg="#101218", highlightthickness=0)
        self.canvas.grid(row=0, column=0, rowspan=2, sticky="n", padx=(0, 14))
        self.canvas_image = self.canvas.create_image(0, 0, anchor="nw")
        self.canvas.bind("<MouseWheel>", self.on_wheel)

        right = ttk.Frame(self)
        right.grid(row=0, column=1, sticky="new")
        right.columnconfigure(0, weight=1)
        ttk.Label(right, text=self.slot["help"], style="Hint.TLabel", wraplength=420, justify="left").grid(row=0, column=0, sticky="w", pady=(0, 8))
        row = ttk.Frame(right)
        row.grid(row=1, column=0, sticky="ew")
        ttk.Button(row, text="Choose picture...", style="Accent.TButton", command=self.choose).pack(side="left")
        ttk.Button(row, text="Use the default again", command=self.remove).pack(side="left", padx=8)
        self.name_label = ttk.Label(right, text="", style="Hint.TLabel")
        self.name_label.grid(row=2, column=0, sticky="w", pady=(4, 8))
        self.fit_box = ttk.Combobox(right, state="readonly", values=list(FIT_LABELS.values()))
        self.fit_box.grid(row=3, column=0, sticky="ew", pady=(0, 6))
        self.fit_box.bind("<<ComboboxSelected>>", lambda _e: self.set_cfg("fit", "fill" if self.fit_box.current() == 0 else "fit"))
        self.zoom = Slider(right, "Zoom", 0.5, 4.0, 0.01, "Mouse wheel over the preview also zooms.", lambda v: self.set_cfg("zoom", v))
        self.zoom.grid(row=4, column=0, sticky="ew")
        self.ox = Slider(right, "Move sideways", -1, 1, 0.01, "Slide the picture left / right inside the frame.", lambda v: self.set_cfg("ox", v))
        self.ox.grid(row=5, column=0, sticky="ew")
        self.oy = Slider(right, "Move up / down", -1, 1, 0.01, "Slide the picture up / down inside the frame.", lambda v: self.set_cfg("oy", v))
        self.oy.grid(row=6, column=0, sticky="ew")
        self.bright = Slider(right, "Brightness", 0.3, 1.5, 0.01, "Darker is easier on the eyes (and keeps the zones readable).", lambda v: self.set_cfg("bright", v))
        self.bright.grid(row=7, column=0, sticky="ew")
        nxt = 8
        if self.slot["border"]:
            self.border = Slider(right, "Black border", 0.0, 0.06, 0.002, "Like the default card back. 0 = none.", lambda v: self.set_cfg("border", v), 3)
            self.border.grid(row=nxt, column=0, sticky="ew")
            nxt += 1
        self.zones_var = tk.BooleanVar(value=True)
        if self.key == "playmat":
            ttk.Checkbutton(right, text="Show the zones in the preview", variable=self.zones_var, command=self.render).grid(row=nxt, column=0, sticky="w", pady=(6, 0))
            nxt += 1
        self.live_var = tk.BooleanVar(value=bool(self.cfg["live"]))
        ttk.Checkbutton(right, text="Apply to the game automatically", variable=self.live_var, command=self.on_live).grid(row=nxt, column=0, sticky="w", pady=(6, 0))
        ttk.Button(right, text="Apply to game now", command=self.apply).grid(row=nxt + 1, column=0, sticky="w", pady=(6, 0))

        self.fit_box.current(0 if self.cfg["fit"] == "fill" else 1)
        self.zoom.set(self.cfg["zoom"])
        self.ox.set(self.cfg["ox"])
        self.oy.set(self.cfg["oy"])
        self.bright.set(self.cfg["bright"])
        if self.slot["border"]:
            self.border.set(self.cfg["border"])
        self.load_source(self.cfg["src"], quiet=True)
        self.loading = False
        self.render()

    # ------------------------------------------------------------ source
    def choose(self):
        filename = filedialog.askopenfilename(title="Pick a picture", filetypes=[("Pictures", "*.png *.jpg *.jpeg *.bmp *.webp *.gif"), ("All files", "*.*")])
        if filename and self.load_source(filename):
            self.cfg.update(zoom=1.0, ox=0.0, oy=0.0)
            self.loading = True
            self.zoom.set(1.0)
            self.ox.set(0.0)
            self.oy.set(0.0)
            self.loading = False
            self.store()
            self.render()
            self.schedule_apply(immediate=True)

    def load_source(self, filename, quiet=False):
        if not filename or not os.path.exists(filename):
            self.src = self.src_small = None
            self.name_label.config(text="No picture chosen: the game's own one is used." if not filename else "The picture %s is gone." % filename)
            return False
        try:
            image = Image.open(filename)
            image.load()
            self.src = image.convert("RGB")
        except Exception as e:
            if not quiet:
                messagebox.showerror("Rarity Studio", "The picture could not be opened:\n%s" % e)
            return False
        self.cfg["src"] = filename
        full = max(self.slot["size"]) * 2
        self.src.thumbnail((full, full), Image.LANCZOS)
        self.src_small = self.src.copy()
        self.src_small.thumbnail((max(self.pw, self.ph) * 2,) * 2, Image.LANCZOS)
        self.name_label.config(text="%s  (%d x %d)" % (os.path.basename(filename), image.size[0], image.size[1]))
        return True

    def remove(self):
        target = os.path.join(self.studio.game_folder.get(), "custom_art", self.slot["file"])
        try:
            if os.path.exists(target):
                os.remove(target)
        except OSError as e:
            messagebox.showerror("Rarity Studio", str(e))
            return
        self.src = self.src_small = None
        self.cfg["src"] = ""
        self.name_label.config(text="No picture chosen: the game's own one is used.")
        self.store()
        self.render()
        self.window.set_status("%s: the default is used again." % self.slot["title"])

    # ------------------------------------------------------------ editing
    def set_cfg(self, key, value):
        if self.loading:
            return
        self.cfg[key] = value
        self.store()
        self.render()
        self.schedule_apply()

    def on_wheel(self, event):
        if self.src is None:
            return
        zoom = min(4.0, max(0.5, self.cfg["zoom"] * (1.05 if event.delta > 0 else 1 / 1.05)))
        self.zoom.set(zoom)
        self.set_cfg("zoom", zoom)

    def on_live(self):
        self.cfg["live"] = self.live_var.get()
        self.store()
        if self.live_var.get():
            self.schedule_apply(immediate=True)

    def store(self):
        self.studio.settings.setdefault("art", {})[self.key] = dict(self.cfg)
        self.studio.store_settings()

    def params(self):
        return dict(zoom=self.cfg["zoom"], ox=self.cfg["ox"], oy=self.cfg["oy"], bright=self.cfg["bright"], fit=self.cfg["fit"],
                    border=self.cfg["border"] if self.slot["border"] else 0.0)

    def render(self):
        image = Image.new("RGB", (self.pw, self.ph), (16, 18, 24))
        if self.src_small is not None:
            image = render_art(self.src_small, (self.pw, self.ph), **self.params())
        elif self.slot["border"]:
            default = os.path.join(self.studio.game_folder.get(), "textures", "cover.png" if self.key == "sleeve" else "cover2.png")
            if not os.path.exists(default):
                default = os.path.join(self.studio.game_folder.get(), "textures", "cover.png")
            try:
                image = Image.open(default).convert("RGB").resize((self.pw, self.ph), Image.LANCZOS)
            except Exception:
                pass
        if self.key == "playmat" and self.zones_var.get():
            zones = os.path.join(self.studio.game_folder.get(), "textures", "field.png")
            try:
                overlay = Image.open(zones).convert("RGBA").resize((self.pw, self.ph), Image.LANCZOS)
                image = image.convert("RGBA")
                image.alpha_composite(overlay)
                image = image.convert("RGB")
            except Exception:
                pass
        self.photo = ImageTk.PhotoImage(image)
        self.canvas.itemconfig(self.canvas_image, image=self.photo)

    # -------------------------------------------------------------- apply
    def schedule_apply(self, immediate=False):
        if not self.live_var.get() or self.src is None:
            return
        if self.job:
            self.after_cancel(self.job)
        self.job = self.after(10 if immediate else 500, self.apply)

    def apply(self):
        self.job = None
        if self.src is None:
            self.window.set_status("Choose a picture first.")
            return
        folder = self.studio.game_folder.get()
        if not os.path.isdir(folder):
            self.window.set_status("The game folder does not exist.")
            return
        try:
            image = render_art(self.src, self.slot["size"], **self.params())
            atomic_save_png(image, os.path.join(folder, "custom_art", self.slot["file"]))
            when = "now" if self.key == "playmat" else "the next time the game starts"
            self.window.set_status("%s saved. The game uses it %s." % (self.slot["title"], when))
        except Exception as e:
            self.window.set_status("Could not save: %s" % e)


class ArtWindow(tk.Toplevel):
    def __init__(self, studio, Slider):
        super().__init__(studio)
        self.studio = studio
        self.title("Sleeves and playmat")
        self.configure(bg=BG)
        self.geometry("760x640")
        book = ttk.Notebook(self)
        book.pack(fill="both", expand=True, padx=10, pady=10)
        self.panels = []
        for key in SLOTS:
            panel = ArtPanel(book, self, key, Slider)
            book.add(panel, text=SLOTS[key]["title"])
            self.panels.append(panel)
        self.status = ttk.Label(self, text="Pick a picture: it is cropped to the right shape for you. Drag the sliders to place it.", style="Hint.TLabel")
        self.status.pack(fill="x", padx=12, pady=(0, 10))

    def set_status(self, text):
        self.status.config(text=text)
        self.studio.set_status(text)


def open_art_window(studio, Slider):
    existing = getattr(studio, "art_window", None)
    if existing is not None and existing.winfo_exists():
        existing.lift()
        return
    studio.art_window = ArtWindow(studio, Slider)

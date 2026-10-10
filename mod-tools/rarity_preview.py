"""
Live preview for the Rarity Studio: draws the same layers the game draws (patterns, tiling, movement, colors, pulse)
on a card, animated. It follows the game's drawing closely but it is an approximation; the game is the final judge.
Needs Pillow and numpy:   pip install pillow numpy
"""
import math
import colorsys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
SPEED = 1.8  # the game moves everything 1.8 times faster than the "per second" numbers
ART = (0.120, 0.880, 0.180, 0.705)
NAME = (0.06, 0.80, 0.030, 0.110)
Y, X = np.mgrid[0:SIZE, 0:SIZE]
Xf, Yf = X.astype(np.float32), Y.astype(np.float32)


# --------------------------------------------------------------- colors
def hsv_arrays(h, s, alpha):
    """Arrays of hue -> (rgb 0..1 as HxWx3, alpha 0..1). Same maths as the game."""
    h = h - np.floor(h)
    c = s
    x = c * (1 - np.abs(np.mod(h * 6, 2.0) - 1))
    m = 1 - c
    sector = (h * 6).astype(int)
    zero = np.zeros_like(h)
    cc = np.full_like(h, c)
    r = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4], [cc, x, zero, zero, x], default=cc)
    g = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4], [x, cc, cc, x, zero], default=zero)
    b = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4], [zero, zero, x, cc, cc], default=x)
    rgb = np.stack([r + m, g + m, b + m], axis=-1)
    return rgb, np.broadcast_to(alpha, h.shape).astype(np.float32)


def hsv_scalar(h, s):
    r, g, b = colorsys.hsv_to_rgb(h - math.floor(h), s, 1.0)
    return r * 255, g * 255, b * 255


# ------------------------------------------------------------- patterns
def _hash(i):
    x = i & 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x7FEB352D) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x846CA68B) & 0xFFFFFFFF
    x ^= x >> 16
    return x


def _glitter():
    alpha = np.zeros((SIZE, SIZE), np.float32)

    def plot(x, y, a):
        x %= SIZE
        y %= SIZE
        if alpha[y, x] < a / 255.0:
            alpha[y, x] = a / 255.0
    for i in range(90):
        cx, cy = _hash(i * 2 + 1) % SIZE, _hash(i * 2 + 2) % SIZE
        arm = 3 + _hash(i + 777) % 6
        power = 150 + _hash(i + 999) % 106
        plot(cx, cy, 255)
        for k in range(1, arm + 1):
            a = power * (arm + 1 - k) // (arm + 1)
            plot(cx + k, cy, a)
            plot(cx - k, cy, a)
            plot(cx, cy + k, a)
            plot(cx, cy - k, a)
        for dx, dy in ((1, 1), (-1, -1), (1, -1), (-1, 1)):
            plot(cx + dx, cy + dy, power // 2)
    return np.ones((SIZE, SIZE, 3), np.float32), alpha


def _white(alpha):
    return np.ones((SIZE, SIZE, 3), np.float32), alpha.astype(np.float32)


def _weave(band_width):
    px, py = (X % 4).astype(np.float32), (Y % 4).astype(np.float32)
    cx, cy = X // 4, Y // 4
    across_h, across_v = np.sin(math.pi * (py + 0.5) / 4.0), np.sin(math.pi * (px + 0.5) / 4.0)
    along_h, along_v = 0.65 + 0.35 * np.sin(math.pi * (px + 0.5) / 4.0), 0.65 + 0.35 * np.sin(math.pi * (py + 0.5) / 4.0)
    top = ((cx + cy) % 2) == 0
    h = across_h * np.where(top, along_h, 0.3)
    v = across_v * np.where(top, 0.3, along_v)
    horizontal = h >= v
    shade = np.maximum(h, v)
    hue = (((X + Y) * 4) % SIZE) / SIZE + np.where(horizontal, 0.0, 0.5)
    if band_width:
        d = np.abs(((X + Y) % SIZE) - 128.0)
        band = np.maximum(0.0, 1.0 - d / band_width)
        shade = band * band * shade
    return hsv_arrays(hue.astype(np.float32), 0.9, np.clip(shade, 0, 1))


def make_pattern(name):
    """(rgb HxWx3 0..1, alpha HxW 0..1) of the 256 pixel tile, like the game's."""
    d = (X + Y) % SIZE
    if name == "FLAT":
        return _white(np.ones((SIZE, SIZE)))
    if name == "SHEEN":
        a = np.maximum(0.0, 1.0 - np.abs(d - 128.0) / 46.0)
        return _white(a * a)
    if name == "GLITTER":
        return _glitter()
    if name == "LINES":
        v = ((X - Y) % 8 + 8) % 8
        hue = d / SIZE
        pulse = 0.65 + 0.35 * np.sin(2 * math.pi * 2 * hue)
        rgb, _ = hsv_arrays(hue.astype(np.float32), 0.75, 1.0)
        return rgb, np.where(v < 2, pulse, 0.0).astype(np.float32)
    if name == "DOTS":
        dx, dy = (X % 8) - 3.5, (Y % 8) - 3.5
        dist = np.sqrt(dx * dx + dy * dy)
        hue = ((X * 7 // 10 + Y) % SIZE) / SIZE
        rgb, _ = hsv_arrays(hue.astype(np.float32), 0.7, 1.0)
        return rgb, np.where(dist < 2.4, 1.0 - dist / 3.2, 0.0).astype(np.float32)
    if name == "CLOUD":
        fx, fy = Xf / SIZE, Yf / SIZE
        n = 0.5 + 0.25 * np.sin(2 * math.pi * (2 * fx + fy)) + 0.15 * np.sin(2 * math.pi * (fx - 3 * fy) + 1.0) + 0.10 * np.sin(2 * math.pi * (5 * fx + 2 * fy) + 2.0)
        n = np.clip(n, 0, 1)
        rgb = np.empty((SIZE, SIZE, 3), np.float32)
        rgb[..., 0], rgb[..., 1], rgb[..., 2] = 220 / 255, 240 / 255, 1.0
        return rgb, (n * n).astype(np.float32)
    if name == "BEAM":
        a = np.maximum(0.0, 1.0 - np.abs(d - 128.0) / 80.0)
        return _white(a * a * (3 - 2 * a))
    if name == "RAYS":
        a = np.zeros((SIZE, SIZE), np.float32)
        for centre, width in zip((20, 70, 120, 175, 225), (10, 26, 8, 20, 14)):
            a = np.maximum(a, np.maximum(0.0, 1.0 - np.abs(d - centre) / width))
        return _white(a)
    if name == "HATCH":
        line = 0.5 + 0.5 * np.cos(2 * math.pi * (X - Y) / 4.0)
        along = np.sin(2 * math.pi * d / SIZE * 2.0)
        pulse = 0.5 + 0.5 * along * along
        return _white(line * line * pulse)
    if name == "VDASH":
        col, row = X % 6, (Y + (X // 6) * 5) % 16
        rgb, _ = hsv_arrays((X / SIZE * 3).astype(np.float32), 0.6, 1.0)
        return rgb, np.where((col < 2) & (row < 9), 1.0, 0.0).astype(np.float32)
    if name == "GLOW":
        dx, dy = (X - 128) / 128.0, (Y - 128) / 128.0
        a = np.maximum(0.0, 1.0 - np.sqrt(dx * dx + dy * dy))
        return _white(a * a)
    if name == "HATCHBAND":
        band = np.maximum(0.0, 1.0 - np.abs(d - 128.0) / 85.0)
        line = 0.5 + 0.5 * np.cos(2 * math.pi * (X - Y) / 4.0)
        hue = (0.1 + 0.35 * d / SIZE) * 2.0
        rgb, _ = hsv_arrays(hue.astype(np.float32), 0.55, 1.0)
        return rgb, (band * band * line * line).astype(np.float32)
    if name == "VDASHBAND":
        band = np.maximum(0.0, 1.0 - np.abs(X - 128.0) / 100.0)
        col, row = X % 4, (Y + (X // 4) * 7) % 12
        hue = ((Y % SIZE) / SIZE + X / SIZE * 0.5) * 2.0
        rgb, _ = hsv_arrays(hue.astype(np.float32), 0.55, 1.0)
        return rgb, np.where((col < 2) & (row < 7), band, 0.0).astype(np.float32)
    if name == "VIGNETTE":
        e = np.maximum(np.abs(X - 127.5) / 127.5, np.abs(Y - 127.5) / 127.5)
        a = np.clip((e - 0.45) / 0.55, 0, 1)
        return _white(a * a)
    if name == "WEAVEBAND":
        return _weave(100.0)
    if name == "WEAVE":
        return _weave(0)
    return _white(np.zeros((SIZE, SIZE)))


_pattern_cache = {}
_scaled_cache = {}


def scaled_pattern(name, size):
    """The tile at `size` pixels (box filtered like the game), as (rgb, alpha) float arrays."""
    key = (name, size)
    hit = _scaled_cache.get(key)
    if hit is not None:
        return hit
    if name not in _pattern_cache:
        rgb, alpha = make_pattern(name)
        rgba = np.concatenate([rgb, alpha[..., None]], axis=-1)
        _pattern_cache[name] = Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), "RGBA")
    img = _pattern_cache[name]
    if name != "FLAT" and size != SIZE:
        img = img.convert("RGBa").resize((size, size), Image.BOX).convert("RGBA")
    arr = np.asarray(img, np.float32) / 255.0
    result = (arr[..., :3], arr[..., 3])
    if len(_scaled_cache) > 120:
        _scaled_cache.clear()
    _scaled_cache[key] = result
    return result


# ---------------------------------------------------------------- layers
def layer_color(layer, seconds):
    r, g, b, a = (int(layer["color"][i:i + 2], 16) for i in (2, 4, 6, 0))
    a = float(a)
    if layer.get("pulse_amp", 0) > 0:
        wave = 0.5 + 0.5 * math.sin(seconds * layer.get("pulse_rate", 0) * 2 * math.pi)
        pa = layer["pulse_amp"]
        a = max(0.0, min(255.0, a * (1.0 - pa + pa * 2 * wave)))
    if layer.get("rainbow"):
        r, g, b = hsv_scalar(seconds * layer.get("hue_rate", 0), layer.get("sat", 0.45))
    elif layer.get("hue_center", -1) >= 0 and layer.get("hue_amp", 0) > 0:
        hue = layer["hue_center"] + layer["hue_amp"] * math.sin(seconds * layer.get("hue_rate", 0) * 2 * math.pi)
        r, g, b = hsv_scalar(hue, layer.get("sat", 0.45))
    return (r / 255.0, g / 255.0, b / 255.0), a / 255.0


def strips(region):
    if region == "BORDER":
        left, right, top, bottom = 0.023, 0.028, 0.016, 0.018
        return [(0, 1, 0, top), (0, 1, 1 - bottom, 1), (0, left, top, 1 - bottom), (1 - right, 1, top, 1 - bottom)]
    if region == "ART":
        return [ART]
    if region == "NAME":
        return [NAME]
    return [(0, 1, 0, 1)]


def _border_layer(out, layer, colour, alpha, seconds):
    """The frame as one band running clockwise round the card, so the pattern goes through the corners without a break."""
    height, width = out.shape[:2]
    rects = []
    for fx0, fx1, fy0, fy1 in strips("BORDER"):  # top, bottom, left, right
        rects.append((int(width * fx0), int(width * fx1), int(height * fy0), int(height * fy1)))
    corner = rects[0][3] - rects[0][2]
    length = [rects[0][1] - rects[0][0], rects[1][1] - rects[1][0], rects[2][3] - rects[2][2], rects[3][3] - rects[3][2]]
    perimeter = sum(length) + 4 * corner
    tile = max(8, int(width * layer["tile"]))
    rounds = max(1, round(perimeter / tile))
    tile = max(8, round(perimeter / rounds))
    tile = min(tile, 1024)
    rgb_tile, a_tile = scaled_pattern(layer["pattern"], tile)
    shift = int(seconds * layer["vx"] * tile)
    start = [0, length[0] + corner + length[3] + corner, length[0] + corner + length[3] + corner + length[1] + corner, length[0] + corner]
    for k in range(4):
        x0, x1, y0, y1 = rects[k]
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            continue
        reversed_ = k in (1, 2)
        vertical = k >= 2
        offset = (shift - start[k] - length[k]) if reversed_ else (start[k] - shift)
        offset %= tile
        xs = np.arange(w) % tile if vertical else (np.arange(w) + offset) % tile
        ys = (np.arange(h) + offset) % tile if vertical else np.arange(h) % tile
        if layer["pattern"] in ("BEAM", "SHEEN", "RAYS"):
            # a band pattern: one line of the tile is stretched across the rim
            if vertical:
                tex_rgb = np.repeat(rgb_tile[ys, 0][:, None, :], w, axis=1)
                tex_a = np.repeat(a_tile[ys, 0][:, None], w, axis=1)[..., None] * alpha
            else:
                tex_rgb = np.repeat(rgb_tile[0, xs][None, :, :], h, axis=0)
                tex_a = np.repeat(a_tile[0, xs][None, :], h, axis=0)[..., None] * alpha
        else:
            tex_rgb = rgb_tile[np.ix_(ys, xs)]
            tex_a = a_tile[np.ix_(ys, xs)][..., None] * alpha
        region = out[y0:y1, x0:x1]
        out[y0:y1, x0:x1] = tex_rgb * np.array(colour, np.float32) * tex_a + region * (1 - tex_a)


def compose(base, layers, seconds):
    """Draw the layers over `base` (HxWx3 float 0..1) and return the new image."""
    out = base.copy()
    height, width = out.shape[:2]
    for layer in layers:
        if not layer.get("enabled", True):
            continue
        colour, alpha = layer_color(layer, seconds)
        if alpha <= 0.002:
            continue
        if layer["region"] == "BORDER" and layer["pattern"] != "FLAT":
            _border_layer(out, layer, colour, alpha, seconds)
            continue
        for fx0, fx1, fy0, fy1 in strips(layer["region"]):
            x0, x1 = int(width * fx0), int(width * fx1)
            y0, y1 = int(height * fy0), int(height * fy1)
            w, h = x1 - x0, y1 - y0
            if w <= 0 or h <= 0:
                continue
            if layer["pattern"] == "FLAT":
                tex_rgb, tex_a = 1.0, 1.0
            else:
                tile = max(8, int(width * layer["tile"]))
                tile = min(tile, 1024)
                rgb, a = scaled_pattern(layer["pattern"], tile)
                off_x = int(((seconds * layer["vx"]) % 1.0) * tile)
                off_y = int(((seconds * layer["vy"]) % 1.0) * tile)
                xs = (np.arange(w) + off_x) % tile
                ys = (np.arange(h) + off_y) % tile
                tex_rgb = rgb[np.ix_(ys, xs)]
                tex_a = a[np.ix_(ys, xs)]
            src = tex_rgb * np.array(colour, np.float32) if not np.isscalar(tex_rgb) else np.array(colour, np.float32)
            a_eff = (tex_a * alpha)
            if np.isscalar(a_eff):
                a_eff = np.float32(a_eff)
            else:
                a_eff = a_eff[..., None]
            region = out[y0:y1, x0:x1]
            out[y0:y1, x0:x1] = src * a_eff + region * (1 - a_eff)
    return out


# ------------------------------------------------------------------ card
FRAMES = {"Effect monster": (200, 120, 60), "Normal monster": (214, 170, 80), "Spell": (30, 140, 120), "Trap": (150, 50, 120), "Fusion": (110, 70, 140), "Link": (30, 80, 140)}


def make_card(width, height, frame="Effect monster", art_image=None):
    """A stand-in card (frame, name bar, picture, text box) as HxWx3 float 0..1."""
    img = Image.new("RGB", (width, height), FRAMES.get(frame, FRAMES["Effect monster"]))
    draw = ImageDraw.Draw(img)
    # darker rim
    draw.rectangle([0, 0, width - 1, height - 1], fill=(104, 112, 138))
    draw.rectangle([int(width * 0.023), int(height * 0.016), int(width * 0.972), int(height * 0.982)], fill=(12, 12, 14))
    draw.rectangle([int(width * 0.035), int(height * 0.026), int(width * 0.962), int(height * 0.972)], fill=FRAMES.get(frame, FRAMES["Effect monster"]))
    # name bar
    nx0, nx1, ny0, ny1 = NAME
    draw.rectangle([int(width * nx0), int(height * ny0), int(width * nx1), int(height * ny1)], fill=(235, 225, 200))
    draw.text((int(width * nx0) + 6, int(height * ny0) + 2), "Card name", fill=(30, 25, 20))
    # picture
    ax0, ax1, ay0, ay1 = ART
    box = (int(width * ax0), int(height * ay0), int(width * ax1), int(height * ay1))
    aw, ah = box[2] - box[0], box[3] - box[1]
    if art_image is not None:
        picture = art_image.convert("RGB").resize((aw, ah), Image.LANCZOS)
    else:
        yy, xx = np.mgrid[0:ah, 0:aw]
        t = yy / ah
        s = xx / aw
        r = 30 + 90 * t + 40 * np.sin(s * 6)
        g = 40 + 60 * (1 - t) + 30 * np.sin(s * 4 + 1)
        b = 90 + 110 * (1 - t) + 40 * np.cos(s * 5)
        picture = Image.fromarray(np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8), "RGB")
        pd = ImageDraw.Draw(picture)
        pd.ellipse([aw * 0.25, ah * 0.12, aw * 0.75, ah * 0.78], fill=(240, 220, 150))
        pd.ellipse([aw * 0.33, ah * 0.22, aw * 0.67, ah * 0.60], fill=(70, 50, 110))
        picture = picture.filter(ImageFilter.GaussianBlur(1.2))
    img.paste(picture, box[:2])
    draw.rectangle(box, outline=(20, 18, 15), width=2)
    # stars row
    for i in range(7):
        cx = int(width * (0.80 - i * 0.075))
        cy = int(height * 0.145)
        draw.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(240, 190, 40), outline=(120, 80, 10))
    # text box
    draw.rectangle([int(width * 0.06), int(height * 0.73), int(width * 0.94), int(height * 0.955)], fill=(238, 224, 190), outline=(25, 20, 15))
    for i in range(5):
        y = int(height * (0.76 + i * 0.038))
        draw.line([int(width * 0.09), y, int(width * (0.91 - (0.2 if i == 4 else 0))), y], fill=(120, 105, 80), width=1)
    return np.asarray(img, np.float32) / 255.0


def to_image(array):
    return Image.fromarray((np.clip(array, 0, 1) * 255).astype(np.uint8), "RGB")

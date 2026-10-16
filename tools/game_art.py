"""Pixel art of the Game page. Original art, drawn here with code: hand-placed pixels, small palettes,
painted highlights and shadows, and ordered dithering for gradients.

Each art pixel is S x S screen pixels (S = 2), so the sprites keep the sizes and anchor points the C code
expects. Light comes from the top left. Every sprite gets a one pixel dark outline.

make_game_assets.py calls build() and turns the result into C arrays.
"""
import math

from PIL import Image

S = 2
OUT = "#12172a"  # outline colour of the sprites

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    """Blend colour a toward b by t (0..1); both are hex strings. Returns a hex string."""
    ca, cb = rgb(a), rgb(b)
    return "#%02x%02x%02x" % tuple(round(ca[i] + (cb[i] - ca[i]) * t) for i in range(3))


def dither(x, y, t):
    """True for a fraction t of the pixels, in a fixed 4x4 pattern."""
    return BAYER[y & 3][x & 3] / 16.0 < t


class Art:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        self.px = self.im.load()

    def put(self, x, y, c, a=255):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[x, y] = rgb(c) + (a,)

    def get(self, x, y):
        return "#%02x%02x%02x" % self.px[x, y][:3]

    def solid(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h and self.px[x, y][3] > 0

    def rect(self, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.put(i, j, c)

    def rows(self, x, y, w, colours):
        """Horizontal bands, one colour per row: a pipe or a girder lit from above."""
        for k, c in enumerate(colours):
            self.rect(x, y + k, w, 1, c)

    def cols(self, x, y, h, colours):
        for k, c in enumerate(colours):
            self.rect(x + k, y, 1, h, c)

    def vgrad(self, x, y, w, h, ramp):
        """Vertical gradient through the ramp, dithered between steps."""
        for j in range(h):
            t = j / max(1, h - 1) * (len(ramp) - 1)
            i = min(int(t), len(ramp) - 2)
            for k in range(w):
                self.put(x + k, y + j, ramp[i + 1] if dither(x + k, y + j, t - i) else ramp[i])

    def ellipse(self, cx, cy, rx, ry, ramp, light=(-0.5, -0.6), keep=None):
        """A lit ball. The ramp runs from shadow to highlight. `keep(x, y)` can cut parts away."""
        lx, ly, lz = light[0], light[1], 0.62
        n = math.sqrt(lx * lx + ly * ly + lz * lz)
        lx, ly, lz = lx / n, ly / n, lz / n
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                nx, ny = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                r2 = nx * nx + ny * ny
                if r2 > 1.0 or (keep and not keep(x, y)):
                    continue
                b = max(0.0, nx * -lx + ny * -ly + math.sqrt(1 - r2) * lz)
                self.put(x, y, ramp[min(len(ramp) - 1, int(b * len(ramp) * 1.1))])

    def outline(self, c=OUT):
        edge = []
        for y in range(self.h):
            for x in range(self.w):
                if not self.solid(x, y) and any(self.solid(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    edge.append((x, y))
        for x, y in edge:
            self.put(x, y, c)
        return self

    def image(self):
        return self.im.resize((self.w * S, self.h * S), Image.NEAREST)


# ---------------------------------------------------------------- hero (22 x 29 art pixels)
SKIN = ["#b9845f", "#d9a678", "#e8b98f", "#f3cfa8"]
BLUE = ["#1d3f8f", "#2557b8", "#3b82f6", "#7ab4ff"]
PANTS = ["#4a5472", "#6b7794", "#8a96b4"]


def hero(spread):
    a = Art(22, 29)
    # jetpack tank, a lit cylinder, with bands, a nozzle and straps to the body
    a.cols(2, 8, 14, ["#4a5168", "#6f7b95", "#aab5cc", "#d4dbea", "#8b93a5", "#4a5168"])
    a.rect(3, 8, 4, 1, "#d4dbea")
    for y in (11, 17):
        a.rect(2, y, 6, 1, "#3a4157")
    a.rect(3, 22, 4, 2, "#2a3146")
    a.rect(4, 23, 2, 1, "#12172a")
    for y in (11, 17):
        a.rect(8, y, 2, 2, "#3a4157")
    # legs: pants lit from the left, dark boots
    for side, x0 in ((0, 10), (1, 15)):
        for y in range(22, 29):
            d = 0
            if spread:
                d = round((y - 22) * (0.5 if side else -0.5))
            for k, c in enumerate([PANTS[2], PANTS[1], PANTS[1], PANTS[0]]):
                a.put(x0 + k + d, y, c if y < 27 else ["#3a4157", "#2a3146", "#2a3146", "#1b2033"][k])
    # vest: lit shoulder, shaded right side and bottom, belt, reflective stripe
    a.rect(9, 10, 11, 13, "#f97316")
    a.rect(9, 10, 11, 1, "#ffb066")
    a.rect(9, 11, 1, 11, "#ff9a45")
    a.rect(18, 11, 2, 11, "#c2570c")
    a.rect(9, 21, 11, 2, "#c2570c")
    a.rect(9, 22, 11, 1, "#8a3a08")
    a.rect(9, 17, 11, 2, "#3b2410")
    a.rect(13, 17, 2, 2, "#f5b921")
    a.rect(10, 13, 8, 1, "#ffe9c4")
    # the arm that reaches forward
    a.rect(16, 12, 4, 3, "#f97316")
    a.rect(16, 12, 4, 1, "#ffb066")
    a.rect(16, 14, 4, 1, "#c2570c")
    a.rect(20, 12, 2, 3, "#e8b98f")
    a.put(20, 12, "#f3cfa8")
    a.put(21, 14, "#b9845f")
    # head: skin, a blue helmet on top, a strap, goggles
    a.ellipse(15, 7, 6, 6, SKIN)
    a.ellipse(15, 7, 6, 6, BLUE, keep=lambda x, y: y <= 6)
    for x in range(9, 22):
        if a.solid(x, 7):
            a.put(x, 7, BLUE[0])
    a.rect(9, 6, 6, 2, "#10244f")
    a.rect(14, 5, 8, 5, "#12172a")
    a.rows(15, 6, 6, ["#d8f6ff", "#7fd3ee", "#2f93b5"])
    a.put(16, 6, "#ffffff")
    a.put(17, 6, "#ffffff")
    a.put(18, 10, "#8a4b3a")
    a.put(19, 10, "#8a4b3a")
    a.put(11, 4, BLUE[3])
    a.put(12, 3, BLUE[3])
    return a.outline()


# ---------------------------------------------------------------- jetpack flame (7 x 18)
def flame(length, lean):
    a = Art(7, 18)
    for r in range(length):
        t = r / length
        hw = 3.2 * (1 - t) ** 0.75
        for x in range(7):
            d = abs(x - 3 - lean * t)
            if d > hw:
                continue
            u = d / max(hw, 0.01)
            if u < 0.35 and t < 0.55:
                c = "#fff7d6"
            elif u < 0.65 and t < 0.8:
                c = "#ffe066"
            elif u < 0.95 or t < 0.5:
                c = "#f59e0b"
            else:
                c = "#e8590c"
            a.put(x, r, c)
    return a


# ---------------------------------------------------------------- coin (8 x 8), a spin in 6 frames
def coin(width):
    a = Art(8, 8)
    ramp = ["#a8700f", "#d9a21b", "#f5b921", "#ffe27a"]
    if width <= 2:
        a.rect(3, 0, 2, 8, "#d9a21b")
        a.rect(3, 0, 1, 8, "#f5b921")
        return a.outline("#7a4b0a")
    a.ellipse(4, 4, width / 2, 4, ramp)
    if width >= 6:
        a.rect(3, 2, 2, 4, "#b8801a")
        a.put(3, 2, "#e2a523")
    if width >= 4:
        a.put(4 - width // 2 + 1, 1, "#fff3b0")
    return a.outline("#7a4b0a")


# ---------------------------------------------------------------- zapper node (12 x 12)
def node(core):
    a = Art(12, 12)
    a.ellipse(6, 6, 5.5, 5.5, ["#1b2236", "#3a4157", "#5d6a88", "#8f9bb8"])
    a.ellipse(6, 6, 2.6, 2.6, [core[0], core[1], core[1]])
    for x, y in ((2, 2), (9, 2), (2, 9), (9, 9)):
        a.put(x, y, "#12172a")
    return a.outline("#0b0d12")


# ---------------------------------------------------------------- missile (23 x 13, nose left)
def missile():
    a = Art(23, 13)
    body = ["#f0f3fa", "#d9dce6", "#aeb4c6", "#8a91a8", "#646b82"]
    for x in range(5, 21):
        for k, c in enumerate(body):
            a.put(x, 3 + k, c)
    for x in range(1, 7):  # the red nose cone
        h = int((x - 0.5) * 0.7) + 1
        for y in range(6 - h, 6 + h + 1):
            t = (y - (6 - h)) / max(1, 2 * h)
            a.put(x, y, ["#ff8a80", "#ef4444", "#c52a2a", "#8f1d1d"][min(3, int(t * 4))])
    a.rect(11, 3, 2, 5, "#ef4444")
    a.rect(11, 3, 1, 5, "#ff8a80")
    for y, c in ((0, "#aab5cc"), (1, "#8b93a5"), (2, "#6b7388")):  # fins
        a.rect(15 + (y == 0), y, 6 - (y == 0), 1, c)
    for y, c in ((12, "#4a5168"), (11, "#6b7388"), (10, "#8b93a5")):
        a.rect(15 + (y == 12), y, 6 - (y == 12), 1, c)
    a.rect(20, 4, 3, 3, "#3a4157")
    a.rect(20, 6, 3, 1, "#2a3146")
    a.put(6, 4, "#ffffff")
    return a.outline()


def missile_flame(length):
    a = Art(10, 6)
    for x in range(length):
        t = x / length
        hw = 2.4 * (1 - t) ** 0.8
        for y in range(6):
            d = abs(y - 2.5)
            if d <= hw:
                a.put(x, y, "#fff7d6" if d < hw * 0.35 and t < 0.5 else ("#ffe066" if d < hw * 0.7 else "#f59e0b"))
    return a


# ---------------------------------------------------------------- warning sign (14 x 13)
def warn():
    a = Art(14, 13)
    for y in range(13):
        hw = 0.5 + y * 0.52
        for x in range(14):
            d = x + 0.5 - 7
            if abs(d) <= hw:
                a.put(x, y, "#ffd75e" if d < -hw + 1.4 else ("#c98f0c" if d > hw - 1.4 else "#f5b921"))
    a.rect(6, 4, 2, 5, "#1a1c22")
    a.rect(6, 10, 2, 2, "#1a1c22")
    return a.outline("#1a1c22")


# ---------------------------------------------------------------- scientist (9 x 16), two strides
def scientist(frame):
    a = Art(9, 16)
    a.ellipse(4.5, 3.5, 2.6, 2.8, SKIN)
    a.rect(2, 1, 5, 1, "#5b4636")
    a.rect(3, 3, 3, 1, "#7fd3ee")
    for y in range(6, 13):  # coat: a trapezoid with a shaded right side
        hw = 2.6 + (y - 6) * 0.25
        for x in range(9):
            d = x + 0.5 - 4.5
            if abs(d) <= hw:
                a.put(x, y, "#aab1c4" if d > hw - 1.2 else ("#f6f8fc" if d < -hw + 1.4 else "#dfe4ef"))
    for k in range(3):  # arms up
        a.put(1 - (k > 1), 6 - k, "#dfe4ef")
        a.put(7 + (k > 1), 6 - k, "#aab1c4")
    a.put(0, 3, "#e8b98f")
    a.put(8, 3, "#e8b98f")
    legs = ((3, 12, 2), (6, 12, -2)) if frame == 0 else ((4, 12, -1), (5, 12, 1))
    for x, y0, lean in legs:
        for k in range(4):
            a.put(x + (lean * k) // 4, y0 + k, "#6b7794" if k < 3 else "#2a3146")
    return a.outline()


# ---------------------------------------------------------------- hero shadow on the floor (22 x 5)
def shadow():
    a = Art(22, 5)
    for y in range(5):
        for x in range(22):
            nx, ny = (x + 0.5 - 11) / 11.0, (y + 0.5 - 2.5) / 2.5
            r = nx * nx + ny * ny
            if r <= 1.0:
                a.put(x, y, "#000000", 215 if r < 0.25 else (150 if r < 0.6 else 80))
    return a


# ---------------------------------------------------------------- ceiling and floor strips (16 x 8)
def ceiling():
    a = Art(16, 8)
    a.rows(0, 0, 16, ["#aab5cc", "#8794b0", "#6f7b95", "#566280", "#566280", "#3c455e", "#232a3e", "#0b0d12"])
    a.rect(0, 0, 1, 7, "#1a2032")
    a.rect(1, 1, 1, 5, "#aab5cc")
    a.rect(7, 2, 2, 2, "#d4dbea")
    a.put(8, 3, "#6f7b95")
    a.rect(7, 4, 2, 1, "#2a3146")
    return a


def floor_plate():
    a = Art(16, 8)
    a.rows(0, 0, 16, ["#0b0d12", "#c3cde3", "#8b97b4", "#6f7b95", "#566280", "#445069", "#2f3850", "#1f2638"])
    for y in range(3, 7):
        for x in range(16):
            if (x + y) % 4 == 0:
                a.put(x, y, "#2a3248")
    a.rect(0, 1, 1, 7, "#141a2a")
    a.rect(1, 2, 1, 5, "#aab5cc")
    a.rect(7, 3, 2, 2, "#d4dbea")
    a.put(8, 4, "#566280")
    return a


# ---------------------------------------------------------------- the lab wall (96 x 146 art pixels)
WALL_RAMP = ["#3d4e72", "#364669", "#2f3d5d", "#283453", "#212b46", "#1b233b"]


def cone(a, cx, y0, y1, half0, spread, strength):
    """A dithered cone of lamp light: the wall inside it is lighter."""
    for y in range(y0, y1):
        hw = half0 + (y - y0) * spread
        for x in range(int(cx - hw) - 1, int(cx + hw) + 2):
            if not (0 <= x < a.w) or abs(x - cx) > hw:
                continue
            p = strength * (1 - (y - y0) / (y1 - y0)) * (1 - 0.6 * abs(x - cx) / hw)
            if dither(x, y, p):
                a.put(x, y, mix(a.get(x, y), "#b8dcff", 0.30))


def wall():
    a = Art(96, 146)
    a.vgrad(0, 0, 96, 146, WALL_RAMP)
    for x in (0, 48):  # panel seams: a dark groove and a lit edge
        a.rect(x, 0, 1, 146, "#141b30")
        a.rect(x + 1, 0, 1, 146, "#4f5f86")
    # ceiling pipes, brackets and a ribbed duct
    a.rows(0, 9, 96, ["#222a40", "#56627f", "#9aa6c3", "#6a7794", "#2a3248"])
    a.rows(0, 15, 96, ["#222a40", "#7a86a4", "#566280", "#2a3248"])
    for x in (15, 63):
        a.rect(x, 8, 4, 12, "#222a40")
        a.rect(x + 1, 8, 1, 12, "#5d6a88")
    a.rect(0, 19, 96, 3, "#2e3752")
    a.rect(0, 19, 96, 1, "#5d6a88")
    for x in range(4, 96, 8):
        a.rect(x, 20, 1, 2, "#171e33")
    # hanging lamps, each with a cone of light
    for cx in (24, 72):
        cone(a, cx, 26, 76, 6, 0.42, 0.5)
        a.rect(cx - 1, 22, 2, 3, "#222a40")
        a.rect(cx - 7, 25, 14, 3, "#dff4ff")
        a.rect(cx - 7, 27, 14, 1, "#7b8bab")
        a.rect(cx - 6, 24, 12, 1, "#9aa6c3")
    # two windows with a skyline
    for x0 in (8, 56):
        y0 = 29
        a.rect(x0 - 3, y0 - 3, 38, 34, "#141b30")
        a.rect(x0 - 2, y0 - 2, 36, 32, "#4a5878")
        a.rect(x0 - 2, y0 - 2, 36, 1, "#aab5cc")
        a.rect(x0 - 2, y0 - 2, 1, 32, "#8a97b5")
        a.vgrad(x0, y0, 32, 28, ["#3a9bc4", "#2379a6", "#14527a", "#0e3a58"])
        for bx, bw, bh, lit in ((2, 5, 9, 0), (9, 4, 14, 1), (13, 6, 11, 0), (20, 4, 17, 1), (25, 6, 8, 0)):
            far = "#14507a" if lit == 0 else "#0c3250"
            a.rect(x0 + bx, y0 + 28 - bh, bw, bh, far)
            for wy in range(y0 + 28 - bh + 2, y0 + 27, 4):
                for wx in range(x0 + bx + 1, x0 + bx + bw - 1, 2):
                    if dither(wx, wy, 0.5):
                        a.put(wx, wy, "#ffe29a")
        for y in range(y0, y0 + 28):  # a soft diagonal reflection
            for x in range(x0, x0 + 32):
                if (x - y) % 14 in (0, 1) and dither(x, y, 0.6):
                    a.put(x, y, mix(a.get(x, y), "#ffffff", 0.22))
        a.rect(x0 + 15, y0, 2, 28, "#8a97b5")
        a.rect(x0, y0 + 13, 32, 2, "#8a97b5")
        a.rect(x0 + 15, y0, 1, 28, "#cdd6ea")
        a.rect(x0 - 3, y0 + 29, 38, 2, "#6a7796")
        a.rect(x0 - 3, y0 + 29, 38, 1, "#aab5cc")
        a.rect(x0 - 3, y0 + 31, 38, 1, "#141b30")
    # a bundle of three pipes with flange rings
    a.rows(0, 66, 96, ["#222a40", "#8f9bb8", "#566280", "#2a3248"])
    a.rows(0, 70, 96, ["#222a40", "#9aa6c3", "#6a7794", "#2a3248"])
    a.rows(0, 74, 96, ["#222a40", "#7a86a4", "#566280", "#222a40"])
    for x in (12, 60):
        a.rect(x, 64, 4, 16, "#2a3248")
        a.rect(x + 1, 64, 1, 16, "#aab5cc")
        a.put(x + 2, 66, "#d4dbea")
        a.put(x + 2, 77, "#d4dbea")
    # machinery: a tank with a valve, a console with a screen, a cabinet
    tank = ["#1b2236", "#3a4560", "#5d6a88", "#8896b6", "#aab5d0", "#8896b6", "#5d6a88", "#3a4560", "#1b2236"]
    for i in range(22):
        t = i / 21.0
        a.rect(6 + i, 94, 1, 33, tank[min(8, int(abs(t - 0.32) * 12) if t < 0.32 else min(8, int(4 + (t - 0.32) * 7)))])
    a.ellipse(17, 94, 11, 3.5, ["#3a4560", "#5d6a88", "#8896b6", "#d4dbea"])
    a.rect(6, 103, 22, 1, "#18203a")
    a.rect(6, 114, 22, 1, "#18203a")
    a.rect(16, 89, 2, 4, "#4a5672")
    a.ellipse(17, 87, 3.5, 3.5, ["#6f7b95", "#aab5cc", "#d4dbea"])
    a.rect(14, 87, 7, 1, "#2a3248")
    a.rect(17, 84, 1, 7, "#2a3248")
    # console
    a.rect(33, 107, 34, 20, "#222b46")
    a.rect(33, 107, 34, 1, "#5b6c93")
    for y in range(98, 107):
        sh = (107 - y)
        a.rect(33 + sh // 3, y, 34 - 2 * (sh // 3), 1, "#2a3452" if y > 98 else "#4a5878")
    a.rect(37, 100, 26, 6, "#0b1426")
    a.rows(38, 101, 24, ["#7DF9FF", "#4cc7e0", "#1c8fb0", "#126f8d"])
    for x in range(39, 52, 2):
        a.put(x, 102, "#ffffff")
    for x in range(40, 58, 3):
        a.put(x, 103, "#d8f6ff")
    for k, c in enumerate(["#ff6a5c", "#7DF9FF", "#9cff8a", "#ffd27a", "#7DF9FF"]):
        a.rect(37 + k * 5, 112, 3, 3, c)
        a.put(37 + k * 5, 112, mix(c, "#ffffff", 0.6))
    a.rect(36, 118, 28, 7, "#18203a")
    for k in range(6):
        a.rect(38 + k * 4, 119, 2, 5, "#0b1020")
    # cabinet
    a.rect(73, 103, 20, 24, "#2a3556")
    a.rect(73, 103, 20, 2, "#6f7ea3")
    a.rect(73, 105, 2, 22, "#3d4a72")
    a.rect(91, 105, 2, 22, "#1f2845")
    a.rect(76, 108, 13, 7, "#18203a")
    a.rect(77, 109, 11, 1, "#7DF9FF")
    a.rect(77, 111, 7, 1, "#4cc7e0")
    a.rect(81, 119, 3, 3, "#ff6a5c")
    a.put(81, 119, "#ffd0ca")
    # baseboard, and the shadow the floor casts on the wall
    a.rect(0, 127, 96, 11, "#141b30")
    a.rect(0, 127, 96, 1, "#4a5878")
    for x in range(0, 96, 16):
        a.rect(x + 1, 129, 14, 6, "#1b2440")
        a.rect(x + 1, 129, 14, 1, "#2e3a5c")
    for y in range(116, 146):
        p = (y - 116) / 30.0 * 0.75
        for x in range(96):
            if dither(x, y, p):
                a.put(x, y, mix(a.get(x, y), "#000000", 0.5))
    return a


def build():
    """Returns {name: (PIL image, opaque)} in draw order."""
    out = {}
    out["wall"] = (wall().image().convert("RGBA"), True)
    out["ceil"] = (ceiling().image(), True)
    out["floor"] = (floor_plate().image(), True)
    out["shadow"] = (shadow().image(), False)
    fly = hero(False).image()
    out["hero_fly"] = (fly, False)
    out["hero_fall"] = (hero(True).image(), False)
    out["hero_dead"] = (fly.rotate(-90, expand=True), False)
    for i, (length, lean) in enumerate(((9, -0.6), (13, 0.5), (17, -0.3))):
        out["flame%d" % i] = (flame(length, lean).image(), False)
    for i, w in enumerate((8, 6, 4, 2, 4, 6)):
        out["coin%d" % i] = (coin(w).image(), False)
    out["node0"] = (node(("#ffe066", "#fff6c2")).image(), False)
    out["node1"] = (node(("#b38f1e", "#ddb64a")).image(), False)
    out["missile"] = (missile().image(), False)
    out["mflame0"] = (missile_flame(6).image(), False)
    out["mflame1"] = (missile_flame(10).image(), False)
    out["warn"] = (warn().image(), False)
    out["sci0"] = (scientist(0).image(), False)
    out["sci1"] = (scientist(1).image(), False)
    return out

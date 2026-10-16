"""Art of the Crossy page. Original voxel art, drawn with code: boxes with three shaded faces, projected
2:1 isometric. build() returns every sprite and the tables the firmware needs.

Axes: a grows up and to the right on screen (forward), b grows down and to the right, z is pixels up.
Projection: x = 16 * (a + b), y = -8 * a + 8 * b - z. Light comes from the top: the top face is the base
colour, the face toward -a is 78 %, the face toward +b is 60 %.

Each sprite has an anchor: the image pixel where model point (a=0, b=0, z=0) lands. The firmware puts a sprite
by placing that pixel on the projected corner of its cell.

make_crossy_assets.py turns the result into C arrays.
"""
import math
from collections import OrderedDict

from PIL import Image, ImageDraw

TW, TH = 16, 8
GROUND_Z = (0, -2, -3, -2)  # lane surface heights (grass, road, river, rail); drawn by src/crossy_ground.c
KEY565 = 0x07E0


def sh(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def project(a, b, z):
    return TW * (a + b), -TH * a + TH * b - z


class Model:
    def __init__(self):
        self.boxes = []

    def box(self, a0, b0, z0, da, db, dz, top, left=None, right=None, layer=0):
        left = left or sh(top, 0.78)
        right = right or sh(top, 0.6)
        self.boxes.append((layer, (b0 + db / 2) - (a0 + da / 2), z0, (a0, b0, z0, da, db, dz), top, left, right))

    def sprite(self):
        pts = [project(0, 0, 0)]  # keep the anchor pixel inside the image
        for _, _, _, (a0, b0, z0, da, db, dz), *_ in self.boxes:
            for a in (a0, a0 + da):
                for b in (b0, b0 + db):
                    for z in (z0, z0 + dz):
                        pts.append(project(a, b, z))
        minx = math.floor(min(p[0] for p in pts))
        miny = math.floor(min(p[1] for p in pts))
        maxx = math.ceil(max(p[0] for p in pts))
        maxy = math.ceil(max(p[1] for p in pts))
        w, h = maxx - minx + 2, maxy - miny + 2
        ax, ay = 1 - minx, 1 - miny
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)

        def P(a, b, z):
            x, y = project(a, b, z)
            return (round(x) + ax, round(y) + ay)

        for _, _, _, (a0, b0, z0, da, db, dz), top, left, right in sorted(self.boxes, key=lambda t: t[:3]):
            a1, b1, zt = a0 + da, b0 + db, z0 + dz
            d.polygon([P(a0, b0, zt), P(a1, b0, zt), P(a1, b1, zt), P(a0, b1, zt)], fill=top + (255,))
            d.polygon([P(a0, b0, zt), P(a0, b1, zt), P(a0, b1, z0), P(a0, b0, z0)], fill=left + (255,))
            d.polygon([P(a0, b1, zt), P(a1, b1, zt), P(a1, b1, z0), P(a0, b1, z0)], fill=right + (255,))
        return im, (ax, ay)


# ---- palette ----

TRUNK = (128, 84, 52)
LEAF = (62, 152, 72)


# ---- scenery ----

def pine():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 6, TRUNK)
    for i, s in enumerate((0.9, 0.7, 0.45)):
        m.box(0.5 - s / 2, 0.5 - s / 2, 6 + i * 6, s, s, 6, sh(LEAF, 1 + 0.1 * i))
    return m.sprite()


def round_tree():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 6, TRUNK)
    m.box(0.1, 0.1, 6, 0.8, 0.8, 13, (74, 170, 84))
    return m.sprite()


def tall_tree():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 8, TRUNK)
    m.box(0.15, 0.15, 8, 0.7, 0.7, 10, (60, 140, 70))
    m.box(0.25, 0.25, 18, 0.5, 0.5, 8, (82, 182, 92))
    return m.sprite()


def rock():
    m = Model()
    m.box(0.15, 0.15, 0, 0.7, 0.7, 5, (160, 162, 172))
    m.box(0.3, 0.3, 5, 0.4, 0.4, 3, (178, 180, 190))
    return m.sprite()


def bush():
    m = Model()
    m.box(0.15, 0.15, 0, 0.7, 0.7, 6, (76, 168, 82))
    m.box(0.3, 0.3, 6, 0.4, 0.4, 2, (92, 188, 96))
    return m.sprite()


def stump():
    m = Model()
    m.box(0.25, 0.25, 0, 0.5, 0.5, 5, (170, 124, 78), TRUNK, sh(TRUNK, 0.8))
    return m.sprite()


def log(length):
    m = Model()
    m.box(0.1, 0, 3, 0.8, length, 5, (150, 100, 55), (125, 82, 45), (214, 170, 110))
    return m.sprite()


def pad():
    m = Model()
    m.box(0.08, 0.08, 3, 0.84, 0.84, 1.5, (78, 190, 92))
    m.box(0.4, 0.4, 4.5, 0.2, 0.2, 1.5, (250, 150, 190))
    return m.sprite()


def rot(face, u0, v0, du, dv):
    """A box given for a hero that looks toward +a (u forward, v right), turned to face 0..3.
    Turns about the centre of a 1 x 1 cell. Returns a0, b0, da, db."""
    pts = []
    for u in (u0, u0 + du):
        for v in (v0, v0 + dv):
            cu, cv = u - 0.5, v - 0.5
            a, b = ((cu, cv), (-cv, cu), (-cu, -cv), (cv, -cu))[face]
            pts.append((a + 0.5, b + 0.5))
    a0 = min(p[0] for p in pts)
    b0 = min(p[1] for p in pts)
    return a0, b0, max(p[0] for p in pts) - a0, max(p[1] for p in pts) - b0


WHITE = (250, 250, 248)
ORANGE = (255, 160, 40)
RED = (232, 44, 44)
DARK = (28, 28, 34)


def hero(face, hop):
    m = Model()

    def rb(u0, v0, z0, du, dv, dz, top, **kw):
        a0, b0, da, db = rot(face, u0, v0, du, dv)
        m.box(a0, b0, z0, da, db, dz, top, **kw)

    lift = 4 if hop else 0
    legs = 1 if hop else 2
    rb(0.3, 0.28, lift, 0.1, 0.1, legs, ORANGE)
    rb(0.3, 0.62, lift, 0.1, 0.1, legs, ORANGE)
    rb(0.2, 0.2, lift + legs, 0.6, 0.6, 12 - (1 if hop else 0), WHITE)
    rb(0.35, 0.12, lift + 5, 0.3, 0.08, 5, sh(WHITE, 0.92))  # wings
    rb(0.35, 0.8, lift + 5, 0.3, 0.08, 5, sh(WHITE, 0.92))
    rb(0.78, 0.3, lift + 9, 0.2, 0.4, 3, ORANGE)               # beak
    rb(0.42, 0.4, lift + legs + 12, 0.16, 0.2, 3, RED)         # comb
    rb(0.72, 0.18, lift + 10, 0.1, 0.1, 2, DARK)               # eyes
    rb(0.72, 0.72, lift + 10, 0.1, 0.1, 2, DARK)
    return m.sprite()


def hero_flat():
    m = Model()
    m.box(0.1, 0.1, 0, 0.8, 0.8, 2, WHITE)
    m.box(0.4, 0.4, 2, 0.2, 0.2, 0.5, RED)
    return m.sprite()


def splash(frame):
    m = Model()
    r = 0.25 + 0.2 * frame
    for k in range(8):
        ang = k * math.pi / 4
        a = 0.5 + r * math.cos(ang) - 0.06
        b = 0.5 + r * math.sin(ang) - 0.06
        m.box(a, b, 3 + (2 if frame == 0 else 0), 0.12, 0.12, 3 + 2 * (1 - frame), (190, 228, 255))
    return m.sprite()


def eagle(frame, carry=False):
    m = Model()
    brown, light = (110, 76, 52), (238, 232, 220)
    m.box(0.3, 0.3, 14, 0.4, 0.5, 6, brown)
    m.box(0.62, 0.4, 18, 0.22, 0.28, 5, light)             # head
    m.box(0.84, 0.46, 19, 0.16, 0.16, 2, (240, 190, 40))   # beak
    wing_z = 20 if frame == 0 else 12
    m.box(-0.5, 0.0, wing_z, 1.0, 0.3, 2, sh(brown, 0.9))
    m.box(-0.5, 0.7, wing_z, 1.0, 0.3, 2, sh(brown, 0.9))
    m.box(0.3, 0.1, 15, 0.4, 0.1, 3, brown)
    if carry:
        m.box(0.3, 0.3, -6, 0.4, 0.4, 7, WHITE)                # chicken hangs below the body
        m.box(0.42, 0.42, 1, 0.16, 0.16, 2, RED)                # its comb
        m.box(0.38, 0.38, 1, 0.06, 0.06, 13, ORANGE)            # talon grip
    return m.sprite()


def animal(kind, frame):
    m = Model()
    bob = frame
    if kind == "sheep":
        m.box(0.2, 0.15, 0, 0.6, 0.7, 7, (246, 246, 240))
        m.box(0.28, 0.8, 3 + bob, 0.44, 0.3, 6, (52, 52, 58))
    elif kind == "pig":
        m.box(0.2, 0.15, 0, 0.6, 0.7, 6, (250, 170, 186))
        m.box(0.28, 0.8, 2 + bob, 0.44, 0.3, 5, (240, 150, 172))
    else:
        m.box(0.15, 0.1, 0, 0.7, 0.8, 9, (246, 246, 246))
        m.box(0.15, 0.3, 0, 0.7, 0.25, 9.2, (62, 52, 52))
        m.box(0.25, 0.9, 4 + bob, 0.5, 0.3, 6, (232, 202, 192))
    return m.sprite()


def coin(frame):
    m = Model()
    face = (0.6, 0.42, 0.14, 0.42)[frame]
    m.box(0.5 - face / 2, 0.44, 5, face, 0.12, 8, (252, 206, 52), (232, 172, 32), (206, 144, 22))
    return m.sprite()


GLASS = ((200, 230, 255), (170, 200, 230), (140, 170, 200))


def mirror_b(m_boxes, length):
    """Mirror a list of (a0, b0, z0, da, db, dz, top, left, right) along b, for the other heading."""
    out = []
    for a0, b0, z0, da, db, dz, top, left, right in m_boxes:
        out.append((a0, length - b0 - db, z0, da, db, dz, top, left, right))
    return out


def vehicle_boxes(kind):
    """Boxes of a vehicle that heads toward +b. Returns (boxes, length in cells)."""
    L = {"sedan": 1.4, "taxi": 1.4, "sport": 1.5, "pickup": 1.7, "truck": 2.2, "bus": 2.6}[kind]
    B = []

    def box(a0, b0, z0, da, db, dz, top, left=None, right=None):
        B.append((a0, b0, z0, da, db, dz, top, left or sh(top, 0.78), right or sh(top, 0.6)))

    def wheels():
        for wb in (0.15, L - 0.45):
            box(0.05, wb, 0, 0.1, 0.3, 3, DARK)
            box(0.85, wb, 0, 0.1, 0.3, 3, DARK)

    def lights():
        box(0.15, L - 0.02, 2, 0.18, 0.04, 1.5, (255, 236, 150))
        box(0.67, L - 0.02, 2, 0.18, 0.04, 1.5, (255, 236, 150))
        box(0.15, -0.02, 2, 0.18, 0.04, 1.5, (210, 40, 40))
        box(0.67, -0.02, 2, 0.18, 0.04, 1.5, (210, 40, 40))

    if kind == "sedan":
        box(0.12, 0, 0, 0.76, L, 6, (226, 52, 52))
        box(0.2, 0.3, 6, 0.6, 0.7, 5, GLASS[0], GLASS[1], GLASS[2])
    elif kind == "taxi":
        box(0.12, 0, 0, 0.76, L, 6, (250, 204, 40))
        box(0.2, 0.3, 6, 0.6, 0.7, 5, GLASS[0], GLASS[1], GLASS[2])
        box(0.4, 0.5, 11, 0.2, 0.3, 2, (250, 250, 250))
    elif kind == "sport":
        box(0.1, 0, 0, 0.8, L, 5, (52, 110, 232))
        box(0.22, 0.4, 5, 0.56, 0.55, 3.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.42, 0.05, 5, 0.16, L - 0.1, 0.5, (240, 240, 240))
    elif kind == "pickup":
        box(0.12, 0, 0, 0.76, L, 5, (60, 160, 90))
        box(0.2, L - 0.8, 5, 0.6, 0.7, 4.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.16, 0.08, 5, 0.68, 0.7, 2, (150, 150, 158))
    elif kind == "truck":
        box(0.12, 0, 0, 0.76, L, 4, (90, 90, 100))
        box(0.12, L - 0.7, 4, 0.76, 0.7, 5, (240, 150, 40))
        box(0.2, L - 0.5, 7, 0.6, 0.3, 2.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.12, 0.02, 4, 0.76, L - 0.8, 8, (240, 240, 244))
    else:  # bus
        box(0.12, 0, 0, 0.76, L, 9, (222, 52, 48))
        box(0.1, 0, 7.8, 0.04, L, 1.2, (246, 246, 246), (240, 240, 240), (214, 214, 214))
        box(0.1, 0.1, 4.5, 0.04, L - 0.2, 2.5, (190, 225, 250), (190, 225, 250), (150, 185, 215))
    wheels()
    lights()
    return B, L


def draw_boxes(boxes):
    m = Model()
    for a0, b0, z0, da, db, dz, top, left, right in boxes:
        m.box(a0, b0, z0, da, db, dz, top, left, right)
    return m.sprite()


def loco(heading):
    B = []
    L = 2.0

    def box(a0, b0, z0, da, db, dz, top):
        B.append((a0, b0, z0, da, db, dz, top, sh(top, 0.78), sh(top, 0.6)))

    box(0.1, 0, 2, 0.8, L, 8, (52, 86, 170))
    box(0.1, 0, 10, 0.8, L, 2, (236, 236, 238))
    box(0.1, 0, 2, 0.8, 0.7, 12, (46, 74, 150))
    box(0.14, 0.5, 8, 0.72, 0.2, 3, GLASS[0])
    box(0.4, 1.2, 12, 0.2, 0.2, 5, DARK)
    box(0.15, L - 0.04, 5, 0.18, 0.05, 2.5, (255, 236, 150))
    box(0.67, L - 0.04, 5, 0.18, 0.05, 2.5, (255, 236, 150))
    box(0.1, L - 0.4, 2, 0.8, 0.4, 1, (200, 50, 50))
    return draw_boxes(B if heading == 0 else mirror_b(B, L))


def carriage():
    m = Model()
    m.box(0.1, 0, 2, 0.8, 2.0, 9, (52, 86, 170), (46, 74, 150), (36, 60, 120))
    m.box(0.1, 0, 11, 0.8, 2.0, 2, (236, 236, 238))
    for k in range(3):
        m.box(0.08, 0.2 + k * 0.6, 5, 0.04, 0.4, 4, (255, 230, 120), layer=1)
    return m.sprite()


def signal(state):
    m = Model()
    head = (52, 52, 60)
    m.box(0.43, 0.43, 0, 0.14, 0.14, 24, (82, 82, 88))
    m.box(0.15, 0.15, 24, 0.7, 0.7, 12, head)
    off, on, core = (84, 56, 60), (255, 40, 40), (255, 240, 150)
    for k in range(2):
        lit = state == k + 1
        c = on if lit else off
        p = 0.24 + k * 0.28  # lamp position along the face, low = left on screen
        m.box(p, 0.85, 26, 0.2, 0.04, 8, c, layer=1)  # +b face (right on screen)
        m.box(0.11, p, 26, 0.04, 0.2, 8, c, layer=1)  # -a face (left on screen)
        if lit:
            m.box(p + 0.05, 0.89, 28, 0.1, 0.03, 4, core, layer=2)
            m.box(0.08, p + 0.05, 28, 0.03, 0.1, 4, core, layer=2)
    return m.sprite()


def build():
    sprites = OrderedDict()
    groups = []

    def add(name, spr):
        sprites[name] = spr
        return name

    props = [None, add("pine", pine()), add("round", round_tree()), add("tall", tall_tree()),
             add("rock", rock()), add("bush", bush()), add("stump", stump())]
    groups.append(("ca_prop", (7,), props))
    groups.append(("ca_log", (3,), [add("log%d" % n, log(n)) for n in (2, 3, 4)]))
    groups.append(("ca_pad", (), [add("pad", pad())]))
    faces = ("f", "r", "b", "l")
    groups.append(("ca_hero", (4, 2), [add("hero_%s%d" % (f, h), hero(i, h)) for i, f in enumerate(faces) for h in (0, 1)]))
    groups.append(("ca_hero_flat", (), [add("hero_flat", hero_flat())]))
    groups.append(("ca_splash", (2,), [add("splash%d" % i, splash(i)) for i in (0, 1)]))
    groups.append(("ca_eagle", (2,), [add("eagle%d" % i, eagle(i)) for i in (0, 1)]))
    groups.append(("ca_eagle_carry", (), [add("eagle_carry", eagle(1, carry=True))]))
    deco = [None, None]
    for k in ("sheep", "pig", "cow"):
        deco += [add("%s%d" % (k, i), animal(k, i)) for i in (0, 1)]
    groups.append(("ca_deco", (4, 2), deco))
    groups.append(("ca_coin", (4,), [add("coin%d" % i, coin(i)) for i in range(4)]))
    veh = []
    for kind in ("sedan", "taxi", "sport", "pickup", "truck", "bus"):
        boxes, length = vehicle_boxes(kind)
        veh.append(add("car_%s_p" % kind, draw_boxes(boxes)))
        veh.append(add("car_%s_m" % kind, draw_boxes(mirror_b(boxes, length))))
    groups.append(("ca_vehicle", (6, 2), veh))
    groups.append(("ca_loco", (2,), [add("loco_p", loco(0)), add("loco_m", loco(1))]))
    groups.append(("ca_carriage", (), [add("carriage", carriage())]))
    groups.append(("ca_signal", (3,), [add("signal%d" % i, signal(i)) for i in range(3)]))
    return sprites, groups

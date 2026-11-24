"""Printable parts of the one-square prototype, stage 1 (hand-moved sled, no motor yet).

    pip install manifold3d trimesh numpy fonttools
    python hardware/rig/make_parts.py          # writes hardware/rig/stl/*.stl

All sizes are in mm and come from the build plan (plans/2026-10-30_realtime-hasami-board.md): a
square of 33.3, milky acrylic 2 on a printed grid 3 thick, the piece magnet about 7 above the
electromagnet, the sensor at the square's centre and the LED 8 off it. Change the numbers in
the first block and run the script again.

Stack (z up, from the desk): frame floor 3, sled ledge 7, electromagnet 20 (ZYE1-P25/20), gap 2,
grid 3, acrylic 2, then the piece's floor 0.6 under its magnets. Magnet to electromagnet: 7.6.
Every height comes from the printed parts, so no measuring is needed. Clearances are generous
(0.3 to 1.0 mm) because the sizes come from datasheets, not from measured parts. No M3 parts:
the electromagnet is held by an M4 screw into its back from below the sled, and the frame can be
screwed to plywood with M4.
"""

import os

import manifold3d as mf
import numpy as np
import trimesh
from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

# ---- Sizes (mm). ----
SQUARE = 33.3
GRID_T = 3.0            # grid plate thickness
ACRYLIC_T = 2.0
GAP = 2.0               # electromagnet top to grid bottom
MAGNET_D, MAGNET_H = 25.0, 20.0   # ZYE1-P25/20
FLOOR_T = 3.0           # frame base plate
SLED_LEDGE = 7.0        # electromagnet seat above the sled's underside (floor 4.4, hollow 2.6)
SLED_FLOOR = 4.4        # the sled's floor; the M4 screw head sinks 3.4 into it from below,
HEAD_POCKET = 3.4       # leaving a 1.0 shelf under the head (an M4 pan head is 2.8 tall)
CHANNEL_W = 33.0        # between the frame walls
WALL_T = 5.0
FRAME_L, FRAME_W = 150.0, 70.0
GRID_L, GRID_W = 120.0, 45.0      # grid plate: three squares plus margins
SENSOR_WIN = (4.6, 3.8)           # DRV5053 TO-92 body 4.00 x 3.15, legs towards +y
SENSOR_SLOT_W = 5.0               # legs and soldered wires to the +y edge
LED_OFFSET = 8.0                  # LED centre from the square centre, towards -y
LED_WIN = (11.6, 8.6)             # AE-WS2812B board 11 x 8
LED_SLOT_W = 5.0                  # its three wires run up the right of the sensor window and join the
LED_SLOT_X0 = SENSOR_SLOT_W / 2 + 2.5  # sensor's slot past it: every wire leaves by the +y edge
PEG_X, PEG_D, PEG_H = 55.0, 3.0, 2.0  # pegs on the walls locate the grid plate
JIG_SLOT_SHIFT = 3.6              # the identification test's offset
# Pieces are shogi-piece pentagons the size of a real fuhyo (27 long, 22.5 wide, 7.75 thick), tip
# towards +y, in two halves of the same outline pressed together: a translucent lower half (the
# light shows as a band round its side) and an opaque upper half with the character. The magnets
# sit in a closed chamber at the centre of the outline's bounding box, on a floor 0.6 thick.
PIECE_W, PIECE_SHOULDER_W = 22.5, 20.0  # width at the bottom edge, at the shoulders
PIECE_SHOULDER, PIECE_L = 20.0, 27.0    # from the bottom edge to the shoulders, to the tip
PIECE_H = 7.75
PIECE_SPLIT = 4.0                 # height of the lower half
PIECE_FLOOR = 0.6                 # under the magnets: two 0.3 layers
TONGUE_H, TONGUE_WALL = 1.8, 1.2  # the upper half's tongue drops into the lower half's recess
PIECE_FIT = 0.2                   # clearance per side round the tongue: its walls never touch
RIB_BITE = 0.25                   # crush ribs on the tongue reach this far into the recess's wall
RIB_W = 1.0                       # rib width at the tongue; the ribs are triangles, tip outwards
SEAM_CHAMFER = 0.4                # V-groove at the seam, for prying the halves apart
# The character engraved in the body's top, to be filled with paint: 歩 for the human's pieces,
# と for the machine's (the back of a fuhyo). Noto Sans JP is under the SIL Open Font License;
# a bold sans keeps every stroke wider than the printer's nozzle.
PIECE_LABELS = {"fu": "歩", "to": "と"}
LABEL_FONT = os.environ.get("LABEL_FONT", "C:/Windows/Fonts/NotoSansJP-VF.ttf")
LABEL_WEIGHT = 700
LABEL_H = 13.0                    # em size; the glyph itself is a little smaller
LABEL_Y = -1.5                    # centre of the em square, below the outline's centre
LABEL_DEPTH = 0.9                 # three 0.3 layers
POCKET_D, POCKET_H = 10.3, 4.2    # two 10 x 2 magnets; 3.4 of it in the lower half, 0.8 in the upper
SPACER_D = 9.8
ACRYLIC_L, ACRYLIC_W = GRID_L - 2.0, GRID_W - 2.0  # 118 x 43: inside the jig's skirt with room
SEG = 96

WALL_TOP = FLOOR_T + SLED_LEDGE + MAGNET_H + GAP   # 32: grid bottom


def box(x0, y0, z0, x1, y1, z1):
    return mf.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(cx, cy, z0, z1, d):
    return mf.Manifold.cylinder(z1 - z0, d / 2, d / 2, SEG).translate([cx, cy, z0])


def frame():
    """Base plate with two walls; the sled runs in the channel between them, the grid plate
    rests on the wall tops, located by four pegs. Vertical marks on the outer faces: square
    centres (deep) and boundaries (shallow). Four 4.5 holes for M4 screws into plywood."""
    hw = CHANNEL_W / 2
    part = box(-FRAME_L / 2, -FRAME_W / 2, 0, FRAME_L / 2, FRAME_W / 2, FLOOR_T)
    for s in (1, -1):
        y0, y1 = (hw, hw + WALL_T) if s > 0 else (-hw - WALL_T, -hw)
        part += box(-FRAME_L / 2, y0, 0, FRAME_L / 2, y1, WALL_TOP)
        for x in (-PEG_X, PEG_X):  # pegs into the grid plate's holes
            part += cyl(x, s * (hw + WALL_T / 2), WALL_TOP - 0.01, WALL_TOP + PEG_H, PEG_D)
        yo = hw + WALL_T if s > 0 else -hw - WALL_T  # outer face
        for k, x in enumerate((-1.5, -1, -0.5, 0, 0.5, 1, 1.5)):
            depth = 1.0 if k % 2 == 1 else 0.5  # centres at -1, 0, 1 squares
            xx = x * SQUARE
            if s > 0:
                part -= box(xx - 0.4, yo - depth, FLOOR_T, xx + 0.4, yo + 1, WALL_TOP)
            else:
                part -= box(xx - 0.4, yo - 1, FLOOR_T, xx + 0.4, yo + depth, WALL_TOP)
    for x in (-FRAME_L / 2 + 8, FRAME_L / 2 - 8):  # holes to screw or clamp it down
        for y in (-FRAME_W / 2 + 6, FRAME_W / 2 - 6):
            part -= cyl(x, y, -1, FLOOR_T + 1, 4.5)
    return part


def grid():
    """Three squares in a row, the prototype square in the middle. Through windows for the
    sensor (centre) and the LED (8 off, towards -y), slots for their wires to the +y edge (the
    LED's runs from the right of its window, past the sensor's), and shallow lines on the top at
    the square boundaries."""
    part = box(-GRID_L / 2, -GRID_W / 2, 0, GRID_L / 2, GRID_W / 2, GRID_T)
    sw, sh = SENSOR_WIN
    part -= box(-sw / 2, -sh / 2, -1, sw / 2, sh / 2, GRID_T + 1)
    x1 = LED_SLOT_X0 + LED_SLOT_W  # one slot for all the wires, from just past the sensor window
    part -= box(-SENSOR_SLOT_W / 2, sh / 2 - 0.1, -1, x1, GRID_W / 2 + 1, GRID_T + 1)
    lw, lh = LED_WIN
    part -= box(-lw / 2, -LED_OFFSET - lh / 2, -1, lw / 2, -LED_OFFSET + lh / 2, GRID_T + 1)
    part -= box(LED_SLOT_X0, -LED_OFFSET - lh / 2, -1, x1, sh / 2, GRID_T + 1)
    for x in (-PEG_X, PEG_X):
        for y in (-(CHANNEL_W / 2 + WALL_T / 2), CHANNEL_W / 2 + WALL_T / 2):
            part -= cyl(x, y, -1, GRID_T + 1, PEG_D + 0.6)
    for x in (-1.5, -0.5, 0.5, 1.5):
        part -= box(x * SQUARE - 0.3, -GRID_W / 2 - 1, GRID_T - 0.6, x * SQUARE + 0.3, GRID_W / 2 + 1, GRID_T + 1)
    for y in (-SQUARE / 2, SQUARE / 2):
        part -= box(-GRID_L / 2 - 1, y - 0.3, GRID_T - 0.6, GRID_L / 2 + 1, y + 0.3, GRID_T + 1)
    return part


def sled():
    """Cup for the electromagnet (it rests on a ledge, its back with the leads and the M4 hole
    over a hollow) with a slot for the leads, and a handle along -x with three notches: a notch
    at the frame's end means the electromagnet is under that square (nearest notch: the square
    on the handle side, then the middle one, then the far one)."""
    base_x, base_y, cup_od = 32.0, 31.0, 31.0
    seat_d = MAGNET_D + 0.6
    top = SLED_LEDGE + 6.0
    part = box(-base_x / 2, -base_y / 2, 0, base_x / 2, base_y / 2, SLED_LEDGE)
    part += cyl(0, 0, 0, top, cup_od)
    part -= cyl(0, 0, SLED_LEDGE, top + 1, seat_d)          # seat
    part -= cyl(0, 0, SLED_FLOOR, SLED_LEDGE + 0.01, 20.0)  # hollow under the back (leads)
    part -= box(-cup_od / 2 - 1, -3.5, SLED_FLOOR, -6, 3.5, top + 1)  # lead slot towards the handle
    part -= cyl(0, 0, -1, SLED_FLOOR + 0.5, 4.5)            # M4 screw into the magnet's back
    part -= cyl(0, 0, -1, HEAD_POCKET, 9.5)                 # its head sunk below the floor
    handle_end = -140.0
    part += box(handle_end, -4, 0, -base_x / 2 + 0.5, 4, 6)
    frame_end = -FRAME_L / 2
    for c in (-SQUARE, 0.0, SQUARE):  # sled centre under the left, middle, right square
        m = frame_end - c
        part -= box(m - 0.5, -5, 5.0, m + 0.5, 5, 7)
    return part


def jig():
    """Laid over the acrylic, its skirt round the grid plate: a slot the size of a piece's
    outline (either way round) and 3.6 longer in x over the prototype square. A piece pushed to the slot's -x end stands on the
    square's centre, pushed to the +x end 3.6 off it (the identification test). A notch in the
    skirt lets the sensor's and the LED's wires out of the grid plate's +y edge."""
    t, skirt, wall = 2.0, GRID_T + ACRYLIC_T, 1.2
    ix, iy = GRID_L / 2 + 0.4, GRID_W / 2 + 0.4
    part = box(-ix - wall, -iy - wall, 0, ix + wall, iy + wall, t)
    part += box(-ix - wall, -iy - wall, -skirt, ix + wall, iy + wall, 0) - box(-ix, -iy, -skirt - 1, ix, iy, 0.01)
    # The piece's bounding box with room, so it fits tip forwards (human) or backwards (machine).
    rx, ry = PIECE_W / 2 + 0.4, PIECE_L / 2 + 0.4
    part -= box(-rx, -ry, -1, JIG_SLOT_SHIFT + rx, ry, t + 1)
    x0, x1 = -SENSOR_SLOT_W / 2 - 1.5, LED_SLOT_X0 + LED_SLOT_W + 1.5  # wire exit
    part -= box(x0, iy - 3, -skirt - 1, x1, iy + 3, 0.0)
    return part


def piece_outline(inset=0.0):
    """The shogi-piece pentagon, centred on its bounding box, shrunk by inset all round."""
    w, s, y0 = PIECE_W / 2, PIECE_SHOULDER_W / 2, -PIECE_L / 2
    pts = [(-w, y0), (w, y0), (s, y0 + PIECE_SHOULDER), (0, y0 + PIECE_L), (-s, y0 + PIECE_SHOULDER)]
    cs = mf.CrossSection([pts])
    return cs.offset(-inset, mf.JoinType.Miter) if inset else cs


def chamfered(z0, z1, c, top=True):
    """The piece outline from z0 to z1, its top (or bottom) edge chamfered by c."""
    a, b = (z0, z1 - c) if top else (z0 + c, z1)
    body = piece_outline().extrude(b - a).translate([0, 0, a])
    edge = piece_outline(c).extrude(0.01).translate([0, 0, z1 - 0.01 if top else z0])
    return mf.Manifold.batch_hull([body, edge])


def piece_base():
    """Translucent lower half. The magnets drop into the chamber from above, onto its floor; the
    upper half's tongue presses into the recess round the chamber's mouth."""
    part = chamfered(0, PIECE_SPLIT, SEAM_CHAMFER)
    part -= piece_outline(TONGUE_WALL).extrude(TONGUE_H + 1.1).translate([0, 0, PIECE_SPLIT - TONGUE_H - 0.1])
    part -= cyl(0, 0, PIECE_FLOOR, PIECE_SPLIT + 1, POCKET_D)
    return part


def tongue():
    """The upper half's tongue, clear of the recess by PIECE_FIT all round, with crush ribs that
    bite RIB_BITE into the recess's wall: their tips squash as the halves are pressed together, so
    the fit is tight whether the printer prints a little fat or thin. Two ribs on each long edge,
    one on each slanted edge; each rib slopes in over its first 0.5 at the free end (lead-in)."""
    inset = TONGUE_WALL + PIECE_FIT
    z0 = PIECE_SPLIT - TONGUE_H
    part = piece_outline(inset).extrude(TONGUE_H + 0.01).translate([0, 0, z0])
    pts = piece_outline(inset).to_polygons()[0]
    ribs = []
    for (x1, y1), (x2, y2) in zip(pts, list(pts[1:]) + [pts[0]]):
        dx, dy = x2 - x1, y2 - y1
        n = np.hypot(dx, dy)
        tx, ty = dx / n, dy / n
        nx, ny = ty, -tx  # outward for a counter-clockwise outline
        if (x1 + x2) * nx + (y1 + y2) * ny < 0:
            nx, ny = -nx, -ny
        for f in ((1 / 3, 2 / 3) if n > 12 else (0.5,)):
            cx, cy = x1 + dx * f, y1 + dy * f
            reach = PIECE_FIT + RIB_BITE

            def tri(r):
                h = RIB_W / 2
                return mf.CrossSection([[(cx - tx * h - nx * 0.3, cy - ty * h - ny * 0.3),
                                         (cx + tx * h - nx * 0.3, cy + ty * h - ny * 0.3),
                                         (cx + nx * r, cy + ny * r)]], mf.FillRule.EvenOdd)
            full = tri(reach).extrude(TONGUE_H - 0.5 + 0.2).translate([0, 0, z0 + 0.5])  # into the body
            tip = tri(-0.05).extrude(0.01).translate([0, 0, z0 + 0.05])
            ribs.append(mf.Manifold.batch_hull([full, tip]))
    for r in ribs:
        part += r
    return part


class _FlatPen(BasePen):
    """Glyph outline to polygons, curves cut into short straight pieces."""

    def __init__(self, glyphset, steps=8):
        super().__init__(glyphset)
        self.contours, self.cur, self.steps = [], [], steps

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.cur.append(tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d
                                  for a, b, c, d in zip(p0, p1, p2, p3)))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.cur.append(tuple(u * u * a + 2 * u * t * b + t * t * c for a, b, c in zip(p0, p1, p2)))

    def _closePath(self):
        if len(self.cur) > 2:
            self.contours.append(self.cur)
        self.cur = []

    _endPath = _closePath


_font = None


def label(ch):
    """The character as a cross-section, LABEL_H em, centred across and at LABEL_Y."""
    global _font
    if _font is None:
        if not os.path.exists(LABEL_FONT):
            raise SystemExit(f"font not found: {LABEL_FONT} (install Noto Sans JP, or set LABEL_FONT)")
        _font = instantiateVariableFont(TTFont(LABEL_FONT), {"wght": LABEL_WEIGHT})
    glyphs = _font.getGlyphSet()
    name = _font.getBestCmap()[ord(ch)]
    pen = _FlatPen(glyphs)
    glyphs[name].draw(pen)
    k = LABEL_H / _font["head"].unitsPerEm
    adv = glyphs[name].width
    mid = (_font["OS/2"].sTypoAscender + _font["OS/2"].sTypoDescender) / 2  # em square's centre
    contours = [[((x - adv / 2) * k, (y - mid) * k + LABEL_Y) for x, y in c] for c in pen.contours]
    return mf.CrossSection(contours, mf.FillRule.NonZero)


def piece_body(ch):
    """Opaque upper half (human: natural or white, machine: black), modelled where it sits on the
    lower half: a tongue below that presses into the lower half's recess and caps the magnet
    chamber, the top edge chamfered 0.6 and the character engraved LABEL_DEPTH into the top.
    Printed upside down, the engraved face on the bed."""
    part = chamfered(PIECE_SPLIT, PIECE_H, 0.6)
    part += tongue()
    part -= cyl(0, 0, PIECE_SPLIT - TONGUE_H - 1, PIECE_FLOOR + POCKET_H, POCKET_D)
    mark = label(ch)
    assert (mark - piece_outline(1.2)).is_empty(), f"{ch} runs into the chamfer"
    return part - mark.extrude(LABEL_DEPTH + 1).translate([0, 0, PIECE_H - LABEL_DEPTH])


def spacer():
    """Fills the chamber above a single magnet, so that it stays on the floor."""
    return cyl(0, 0, 0, 2.0, SPACER_D)


PARTS = {
    "frame": frame,
    "grid": grid,
    "sled": sled,
    "piece_base": piece_base,
    "piece_body_fu": lambda: piece_body(PIECE_LABELS["fu"]),
    "piece_body_to": lambda: piece_body(PIECE_LABELS["to"]),
    "spacer": spacer,
    "jig": jig,
}


# Printed upside down (the jig's skirt and the upper halves' tongues point up, so nothing needs
# support; the engraved faces lie on the bed).
UPSIDE_DOWN = {"jig", "piece_body_fu", "piece_body_to"}


def write_acrylic(out):
    """Outline of the milky acrylic sheet for a laser cutter: SVG (mm) and DXF (R12, mm)."""
    L, W = ACRYLIC_L, ACRYLIC_W
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{L + 10:g}mm" height="{W + 10:g}mm" '
           f'viewBox="0 0 {L + 10:g} {W + 10:g}">\n'
           f'  <!-- milky acrylic 2 mm, {L:g} x {W:g} mm: cut along the red line -->\n'
           f'  <rect x="5" y="5" width="{L:g}" height="{W:g}" fill="none" stroke="#ff0000" stroke-width="0.1"/>\n'
           f'</svg>\n')
    with open(os.path.join(out, "acrylic.svg"), "w") as f:
        f.write(svg)
    pts = [(0, 0), (L, 0), (L, W), (0, W)]
    lines = ["0", "SECTION", "2", "ENTITIES"]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        lines += ["0", "LINE", "8", "CUT", "10", f"{x1:g}", "20", f"{y1:g}", "30", "0",
                  "11", f"{x2:g}", "21", f"{y2:g}", "31", "0"]
    lines += ["0", "ENDSEC", "0", "EOF"]
    with open(os.path.join(out, "acrylic.dxf"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"acrylic     {L:6.1f} x {W:6.1f} mm (laser outline) -> {os.path.relpath(out)}/acrylic.svg, acrylic.dxf")


def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")
    os.makedirs(out, exist_ok=True)
    os.makedirs(os.path.join(os.path.dirname(out), "laser"), exist_ok=True)
    for name, make in PARTS.items():
        m = make()
        if name in UPSIDE_DOWN:
            m = m.rotate([180, 0, 0])
        lo = m.bounding_box()[2]
        m = m.translate([0, 0, -lo])  # printed from z = 0, as oriented here
        t = to_trimesh(m)
        assert t.is_watertight, name
        path = os.path.join(out, name + ".stl")
        t.export(path)
        ext = t.bounds[1] - t.bounds[0]
        print(f"{name:11s} {ext[0]:6.1f} x {ext[1]:6.1f} x {ext[2]:5.1f} mm  {t.volume / 1000:6.1f} cm3  -> {os.path.relpath(path)}")
    write_acrylic(os.path.join(os.path.dirname(os.path.abspath(__file__)), "laser"))
    print(f"grid bottom (wall top) at z = {WALL_TOP:.1f} mm; magnet to electromagnet = "
          f"{WALL_TOP + GRID_T + ACRYLIC_T + PIECE_FLOOR - (FLOOR_T + SLED_LEDGE + MAGNET_H):.1f} mm")


if __name__ == "__main__":
    main()

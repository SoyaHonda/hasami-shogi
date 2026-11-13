"""Printable parts of the one-square prototype, stage 1 (hand-moved sled, no motor yet).

    pip install manifold3d trimesh numpy
    python hardware/rig/make_parts.py          # writes hardware/rig/stl/*.stl

All sizes are in mm and come from the build plan (plans/2026-10-30_realtime-hasami-board.md): a
square of 33.3, milky acrylic 2 on a printed grid 3 thick, the piece magnet about 7 above the
electromagnet, the sensor at the square's centre and the LED 8 off it. Change the numbers in
the first block and run the script again.

Stack (z up, from the desk): frame floor 3, sled ledge 7, electromagnet 20 (ZYE1-P25/20), gap 2,
grid 3, acrylic 2, then the piece's magnets flush with its bottom. Magnet to electromagnet: 7.
Every height comes from the printed parts, so no measuring is needed. Clearances are generous
(0.3 to 1.0 mm) because the sizes come from datasheets, not from measured parts. No M3 parts:
the electromagnet is held by an M4 screw into its back from below the sled, and the frame can be
screwed to plywood with M4.
"""

import os

import manifold3d as mf
import numpy as np
import trimesh

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
PIECE_BASE_D, PIECE_BASE_H = 28.0, 8.0
PIECE_BODY_D, PIECE_BODY_H = 22.0, 14.0
POCKET_D, POCKET_H = 10.3, 6.2    # three 10 x 2 magnets
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
    """Laid over the acrylic, its skirt round the grid plate: a slot one piece base wide and
    3.6 longer over the prototype square. A piece pushed to the slot's -x end stands on the
    square's centre, pushed to the +x end 3.6 off it (the identification test). A notch in the
    skirt lets the sensor's and the LED's wires out of the grid plate's +y edge."""
    t, skirt, wall = 2.0, GRID_T + ACRYLIC_T, 1.2
    ix, iy = GRID_L / 2 + 0.4, GRID_W / 2 + 0.4
    part = box(-ix - wall, -iy - wall, 0, ix + wall, iy + wall, t)
    part += box(-ix - wall, -iy - wall, -skirt, ix + wall, iy + wall, 0) - box(-ix, -iy, -skirt - 1, ix, iy, 0.01)
    r = PIECE_BASE_D / 2 + 0.4
    slot = cyl(0, 0, -1, t + 1, 2 * r) + cyl(JIG_SLOT_SHIFT, 0, -1, t + 1, 2 * r) + box(0, -r, -1, JIG_SLOT_SHIFT, r, t + 1)
    part -= slot
    x0, x1 = -SENSOR_SLOT_W / 2 - 1.5, LED_SLOT_X0 + LED_SLOT_W + 1.5  # wire exit
    part -= box(x0, iy - 3, -skirt - 1, x1, iy + 3, 0.0)
    return part


def piece_base():
    """Translucent base (light ring). Magnets go in from the bottom, flush with it."""
    part = cyl(0, 0, 0, PIECE_BASE_H, PIECE_BASE_D)
    part -= cyl(0, 0, -1, POCKET_H, POCKET_D)
    part -= cyl(0, 0, PIECE_BASE_H - 0.8, PIECE_BASE_H + 1, PIECE_BODY_D + 0.4)
    return part


def piece_body():
    """Opaque body (human: natural or white, machine: black), glued into the base's recess."""
    part = cyl(0, 0, 0, PIECE_BODY_H - 1.0, PIECE_BODY_D)
    part += mf.Manifold.cylinder(1.0, PIECE_BODY_D / 2, PIECE_BODY_D / 2 - 1.0, SEG).translate([0, 0, PIECE_BODY_H - 1.0])
    return part


def spacer():
    """Fills the pocket above the magnets: one per magnet fewer than three."""
    return cyl(0, 0, 0, 2.0, SPACER_D)


PARTS = {
    "frame": frame,
    "grid": grid,
    "sled": sled,
    "piece_base": piece_base,
    "piece_body": piece_body,
    "spacer": spacer,
    "jig": jig,
}


# Printed upside down (the jig's skirt points up, so nothing needs support).
UPSIDE_DOWN = {"jig"}


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
          f"{WALL_TOP + GRID_T + ACRYLIC_T - (FLOOR_T + SLED_LEDGE + MAGNET_H):.1f} mm")


if __name__ == "__main__":
    main()

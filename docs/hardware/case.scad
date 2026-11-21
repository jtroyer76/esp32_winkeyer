// Keyer case in the form of an M5 Atomic base (cf. ATOM PWM base, docs/reference/M5_Atom_PWM_base_drawing.pdf:
// 24 wide, R3 corners, the Atom sits on a low step at one end and its top is flush with the tall end,
// keyhole mount on the bottom). Ours is longer (about 60 mm) because the carrier board is 22 rows.
//
// Orientation: USB-C to the RIGHT, viewed from above. KEY jack (to the radio) out the top/far wall,
// PADDLE jack out the bottom/near wall, toward you.
//
// Coordinates (seen from above, z up): y = 0 at the USB-C end, z = 0 at the bottom.
// x = 0 is the PADDLE (column 1) wall, x = outer_w the KEY (column 7) wall. Check: with y = 0 at the bottom of
// the screen the key wall is on the right; turn 90° counterclockwise so USB points right and the key wall is on top.
// Two parts: the tray (print open side up) and the lid over the jack end (print flat).
// The carrier hangs from the Atom's headers and rests on a ledge under row V plus two posts.
//
// FIRST PRINT IS A TEST FIT. Check the values marked MEASURE against the built board.

part = "both";          // "tray", "lid" or "both"

// ---- Board (MEASURE) ----
pitch        = 2.54;
board_w      = 20.87;         // measured 2026-11-21 (wider than 7 columns; margin assumed equal both sides)
board_l      = 56.84;         // measured
board_t      = 1.17;          // measured
col_margin   = (board_w - 7 * pitch) / 2;                // long edge to the edge of column 1's pitch cell (assumed equal;
                                                         // headers are in cols 1 and 7, centred on the Atom)
board_gap    = 2.5;           // under-board clearance; solder sticks out 1.5-2 mm (measured)

// ---- Atom (MEASURE atom_standoff) ----
atom_w       = 24;
atom_l       = 24;
atom_h       = 10;
atom_standoff = 3.68 - 1.17;  // measured: board bottom to Atom bottom 3.68, minus the board
atom_overhang = 2;            // measured: the Atom sticks 2 mm past the board's USB-end edge
atom_pin1_y  = 4.5;           // measured: Atom USB-end face to the centre of its first header pin (row A)

// ---- Case ----
outer_w      = 26;            // 24 like the Atom leaves 1.27 mm walls around the 20.87 mm board; 26 gives 2 mm
corner_r     = 3;             // R3 like the M5 bases
floor_t      = 2;
end_wall     = 1.6;           // wall under the Atom at the USB end
clr          = 0.3;           // board clearance all round
far_wall     = 2;

// ---- Jacks (Kycon STX-3120: hole centre 6.5 mm above the board (measured)) ----
jack_proud   = 0.5;           // measured: the jack front sticks 0.5 mm past the board's long edge
plug_d       = 6.75;          // measured: plug body diameter
jack_hole_d  = max(plug_d, 6.82) + 0.6;  // straight through the wall: the plug body reaches the jack front and seats fully
jack_body_w  = 7.4;           // relief in the inner wall for the jack body (6.82 measured)
jack_z_above_board = 6.5;     // measured 2026-11-21 directly (matches the datasheet)
paddle_row   = 11 + 2;        // N: paddle jack (rows L-P, middle), opens on the x = 0 wall (bottom, toward you)
key_row      = 16 + 2;        // S: key jack (rows Q-U, far end), opens on the x = outer_w wall (top, USB right)

// ---- Keyhole on the bottom, like the M5 base (3.2 mm slot, 6 mm head) ----
keyhole      = true;
keyhole_d    = 3.2;
keyhole_head = 6;
keyhole_len  = 5;

// ---- Lid ----
lid_t        = 1.6;
lid_clr      = 0.25;

// ---- Derived ----
atom_y0      = end_wall + clr;                      // Atom's USB-end face
board_y0     = atom_y0 + atom_overhang;
board_x0     = (outer_w - board_w) / 2;
board_bot_z  = floor_t + board_gap;
board_top_z  = board_bot_z + board_t;
atom_bot_z   = board_top_z + atom_standoff;
outer_h      = atom_bot_z + atom_h;                 // flush with the Atom top
outer_l      = board_y0 + board_l + clr + far_wall;
inner_w      = board_w + 2 * clr;
pocket_end_y = atom_y0 + atom_l + 0.3;              // walls step down until here
jack_z       = board_top_z + jack_z_above_board;
wall_t       = board_x0 - clr;
function row_y(r) = atom_y0 + atom_pin1_y + r * pitch;   // rows located from the Atom's first header pin
function col_x(c) = board_x0 + col_margin + pitch / 2 + (c - 1) * pitch;   // col 1 nearest x = 0, col 7 nearest x = outer_w

$fn = 48;

module rounded_box(w, l, h, r) {
    hull() for (x = [r, w - r], y = [r, l - r]) translate([x, y, 0]) cylinder(r = r, h = h);
}

module tray() {
    difference() {
        rounded_box(outer_w, outer_l, outer_h, corner_r);
        // cavity
        translate([board_x0 - clr, board_y0 - clr, floor_t])
            cube([inner_w, board_l + 2 * clr, outer_h]);
        // Atom step: full width from the USB end, down to the Atom's underside
        translate([-1, -1, atom_bot_z]) cube([outer_w + 2, pocket_end_y + 1, outer_h]);
        // lid rebate over the jack end
        translate([board_x0 - clr - 1, pocket_end_y - 0.01, outer_h - lid_t])
            cube([inner_w + 2, outer_l - far_wall + 1 - pocket_end_y, lid_t + 1]);
        // PADDLE jack: x = 0 wall, row N. Plug hole straight through, plus a relief for the jack body sticking past the board edge.
        translate([-1, row_y(paddle_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([board_x0 - jack_proud - 0.3, row_y(paddle_row) - jack_body_w / 2, board_top_z - 0.5])
            cube([jack_proud + 0.5, jack_body_w, jack_z - board_top_z + jack_body_w / 2 + 0.5]);
        // KEY jack: x = outer_w wall, row S
        translate([outer_w - wall_t - 1, row_y(key_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([board_x0 + board_w - 0.2, row_y(key_row) - jack_body_w / 2, board_top_z - 0.5])
            cube([jack_proud + 0.5, jack_body_w, jack_z - board_top_z + jack_body_w / 2 + 0.5]);
        // keyhole under rows G-H (no solder joints there); the screw head ends up inside, under the board
        if (keyhole) translate([outer_w / 2, row_y(6), -0.01]) {
            cylinder(d = keyhole_head, h = floor_t + 0.02);
            translate([-keyhole_d / 2, 0, 0]) cube([keyhole_d, keyhole_len, floor_t + 0.02]);
        }
    }
    // ledge under row V (no joints there) at the far end
    translate([board_x0, row_y(21) - 1, floor_t]) cube([board_w, 2, board_gap]);
    // posts under the jacks at column 7 (N7, S7 have no joints; key pin 1 is at S6)
    for (r = [key_row, paddle_row])
        translate([col_x(7), row_y(r), floor_t]) cylinder(d = 2, h = board_gap);
}

module lid() {
    // drops into the rebate; its top ends flush with the Atom top
    cube([inner_w + 2 - 2 * lid_clr, outer_l - far_wall + 1 - pocket_end_y - lid_clr, lid_t]);
}

if (part == "tray" || part == "both") tray();
if (part == "lid"  || part == "both") translate([outer_w + 6, 0, 0]) lid();

echo(str("Wall at the jacks ", wall_t, " mm; Outside ", outer_w, " x ", outer_l, " x ", outer_h, " mm; step (Atom underside) at z = ", atom_bot_z,
         "; jack centres at z = ", jack_z, "; plug hole top at z = ", jack_z + jack_hole_d / 2, " (lid rebate at ", outer_h - lid_t, ")"));

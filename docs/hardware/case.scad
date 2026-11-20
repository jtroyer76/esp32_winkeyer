// Keyer case in the form of an M5 Atomic base (cf. ATOM PWM base, docs/reference/M5_Atom_PWM_base_drawing.pdf:
// 24 wide, R3 corners, the Atom sits on a low step at one end and its top is flush with the tall end,
// keyhole mount on the bottom). Ours is longer (about 60 mm) because the carrier board is 22 rows.
//
// Orientation: USB-C to the RIGHT, viewed from above. KEY jack (to the radio) out the top/far wall,
// PADDLE jack out the bottom/near wall, toward you.
//
// Coordinates: y = 0 at the USB-C end, x = 0 on the far (KEY, column 7) side, z = 0 at the bottom.
// Two parts: the tray (print open side up) and the lid over the jack end (print flat).
// The carrier hangs from the Atom's headers and rests on a ledge under row V plus two posts.
//
// FIRST PRINT IS A TEST FIT. Check the values marked MEASURE against the built board.

part = "both";          // "tray", "lid" or "both"

// ---- Board (MEASURE) ----
pitch        = 2.54;
board_w      = 7 * pitch;     // 17.8 mm, columns 1-7
board_l      = 22 * pitch;    // 55.9 mm, rows A-V
board_t      = 1.6;
row_a_y      = pitch / 2;     // row A centre from the board's USB-end edge
board_gap    = 3;             // solder-joint clearance under the board

// ---- Atom (MEASURE atom_standoff) ----
atom_w       = 24;
atom_l       = 24;
atom_h       = 10;
atom_standoff = 2.5;          // board top to the underside of the Atom body (header plastic)

// ---- Case ----
outer_w      = 24;
corner_r     = 3;             // R3 like the M5 bases
floor_t      = 2;
end_wall     = 1.6;           // wall under the Atom at the USB end
clr          = 0.3;           // board clearance all round
far_wall     = 2;

// ---- Jacks (Kycon STX-3120: hole centre 6.5 mm above the board, face about 0.9 mm inside the board edge) ----
jack_hole_d  = 7.0;           // clears the jack nose and a typical plug barrel
jack_spot_d  = 10;            // outside counterbore so the plug seats fully
jack_wall_left = 1.2;         // wall left at the jack after the counterbore (face is ~1.2 mm in from the inner wall)
jack_z_above_board = 6.5;
key_row      = 11 + 2;        // N: key jack (rows L-P), opens on the x = 0 wall
paddle_row   = 16 + 2;        // S: paddle jack (rows Q-U), opens on the x = outer_w wall

// ---- Keyhole on the bottom, like the M5 base (3.2 mm slot, 6 mm head) ----
keyhole      = true;
keyhole_d    = 3.2;
keyhole_head = 6;
keyhole_len  = 5;

// ---- Lid ----
lid_t        = 1.6;
lid_clr      = 0.25;

// ---- Derived ----
board_y0     = end_wall + clr;
board_x0     = (outer_w - board_w) / 2;
board_bot_z  = floor_t + board_gap;
board_top_z  = board_bot_z + board_t;
atom_bot_z   = board_top_z + atom_standoff;
outer_h      = atom_bot_z + atom_h;                 // flush with the Atom top
outer_l      = board_y0 + board_l + clr + far_wall;
inner_w      = board_w + 2 * clr;
pocket_end_y = atom_l + 0.3;                        // walls step down until here
jack_z       = board_top_z + jack_z_above_board;
wall_t       = board_x0 - clr;
function row_y(r) = board_y0 + row_a_y + r * pitch;
function col_x(c) = board_x0 + pitch / 2 + (7 - c) * pitch;   // col 7 nearest x = 0

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
        // KEY jack: x = 0 wall, row N
        translate([-1, row_y(key_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([-1, row_y(key_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_spot_d, h = 1 + wall_t - jack_wall_left);
        // PADDLE jack: x = outer_w wall, row S
        translate([outer_w - wall_t - 1, row_y(paddle_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([outer_w - (wall_t - jack_wall_left), row_y(paddle_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_spot_d, h = wall_t);
        // keyhole under rows G-H (no solder joints there); the screw head ends up inside, under the board
        if (keyhole) translate([outer_w / 2, row_y(6), -0.01]) {
            cylinder(d = keyhole_head, h = floor_t + 0.02);
            translate([-keyhole_d / 2, 0, 0]) cube([keyhole_d, keyhole_len, floor_t + 0.02]);
        }
    }
    // ledge under row V (no joints there) at the far end
    translate([board_x0, row_y(21) - 1, floor_t]) cube([board_w, 2, board_gap]);
    // posts under the jacks at column 7 (N7, S7 have no joints)
    for (r = [key_row, paddle_row])
        translate([col_x(7), row_y(r), floor_t]) cylinder(d = 2, h = board_gap);
}

module lid() {
    // drops into the rebate; its top ends flush with the Atom top
    cube([inner_w + 2 - 2 * lid_clr, outer_l - far_wall + 1 - pocket_end_y - lid_clr, lid_t]);
}

if (part == "tray" || part == "both") tray();
if (part == "lid"  || part == "both") translate([outer_w + 6, 0, 0]) lid();

echo(str("Outside ", outer_w, " x ", outer_l, " x ", outer_h, " mm; step (Atom underside) at z = ", atom_bot_z,
         "; jack centres at z = ", jack_z));

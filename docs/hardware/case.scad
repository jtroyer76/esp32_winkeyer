// Keyer case in the form of an M5 Atomic base (cf. ATOM PWM base, docs/reference/M5_Atom_PWM_base_drawing.pdf:
// 24 wide, R3 corners, the Atom sits on a low step at one end and its top is flush with the tall end,
// no mounting hole: it sits on the desk). Ours is longer (about 63 mm) because the carrier board is 22 rows.
//
// Orientation: USB-C to the RIGHT, viewed from above. KEY jack (to the radio) out the top/far wall,
// PADDLE jack out the bottom/near wall, toward you.
//
// Coordinates (seen from above, z up): y = 0 at the USB-C end, z = 0 at the bottom.
// x = 0 is the PADDLE (column 1) wall, x = outer_w the KEY (column 7) wall. Check: with y = 0 at the bottom of
// the screen the key wall is on the right; turn 90° counterclockwise so USB points right and the key wall is on top.
// Two parts: the tray (print open side up) and the lid over the jack end (print top face down, snap skirts up).
// The lid snaps in with two tabs; lift it at the notch in the far end.
// The board sits on rails under both long edges (outside the solder joints), and a ledge under row V.
// The Atom step is 0.2 mm low so the board, not the Atom, sets the height.
//
// FIRST PRINT IS A TEST FIT. Check the values marked MEASURE against the built board.

part = "both";          // "tray", "lid", "both" or "assembled" (check only)

// ---- Board (MEASURE) ----
pitch        = 2.54;
board_w      = 20.87;         // measured 2026-11-21 (wider than 7 columns; margin assumed equal both sides)
board_l      = 56.84;         // measured
board_t      = 1.17;          // measured
col_margin   = (board_w - 7 * pitch) / 2;                // long edge to the edge of column 1's pitch cell (assumed equal;
                                                         // headers are in cols 1 and 7, centred on the Atom)
rail_w       = 1.2;           // support rails under the board's long edges
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

// ---- Lid ----
lid_t        = 1.6;
lid_clr      = 0.25;
// Snap tabs: a flexible skirt hangs from each long edge of the lid; a ridge on it clicks into a groove in the wall.
// Placed diagonally, clear of the jacks: paddle wall at rows Q-U, key wall at rows J-M.
snap_h       = 5;             // skirt height below the lid (long enough to flex without cracking PETG)
snap_t       = 1.0;           // skirt thickness
snap_gap     = 0.15;          // skirt to wall
snap_bump    = 0.45;          // ridge height: 0.3 mm interference while pushing in
snap_groove  = 0.5;           // groove depth in the wall

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
lid_z        = outer_h - lid_t;                     // underside of the lid
snap_zb      = lid_z - snap_h + 1.2;                // ridge peak height
snap_paddle  = [pocket_end_y + 19.5, pocket_end_y + 32.2];   // y range, paddle (x = 0) wall
snap_key     = [pocket_end_y + 1, pocket_end_y + 12];        // y range, key (x = outer_w) wall
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
        translate([-1, -1, atom_bot_z - 0.2]) cube([outer_w + 2, pocket_end_y + 1, outer_h]);
        // snap grooves (the key side is mirrored)
        snap_groove_cut(snap_paddle);
        translate([outer_w, 0, 0]) mirror([1, 0, 0]) snap_groove_cut(snap_key);
        // pry notch in the far wall to lift the lid
        translate([outer_w / 2 - 3, outer_l - far_wall - 0.5, lid_z - 0.8]) cube([6, far_wall + 1, lid_t + 1]);
        // lid rebate over the jack end
        translate([board_x0 - clr - 1, pocket_end_y - 0.01, outer_h - lid_t])
            cube([inner_w + 2, outer_l - far_wall + 1 - pocket_end_y, lid_t + 1]);
        // PADDLE jack: x = 0 wall, row N. Plug hole straight through, plus a slot for the jack body (0.5 mm past the board edge)
        // running up to the top, so the board lowers straight in with the jacks sliding down the slots.
        translate([-1, row_y(paddle_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([board_x0 - jack_proud - 0.3, row_y(paddle_row) - jack_body_w / 2, board_top_z - 0.5])
            cube([jack_proud + 0.5, jack_body_w, outer_h]);   // slot to the top: the board drops straight in
        // KEY jack: x = outer_w wall, row S
        translate([outer_w - wall_t - 1, row_y(key_row), jack_z]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall_t + 2);
        translate([board_x0 + board_w - 0.2, row_y(key_row) - jack_body_w / 2, board_top_z - 0.5])
            cube([jack_proud + 0.5, jack_body_w, outer_h]);   // slot to the top: the board drops straight in
    }
    // rails under both long edges, full length: the nearest joints (cols 1 and 7) are 2.8 mm in from the edge
    for (x = [board_x0, board_x0 + board_w - rail_w])
        translate([x, board_y0, floor_t]) cube([rail_w, board_l, board_gap]);
    // ledge under row V (no joints there) at the far end
    translate([board_x0, row_y(21) - 1, floor_t]) cube([board_w, 2, board_gap]);
}

// Skirt profile in the x-z plane for the x = 0 wall: the ridge points toward the wall.
// Long slope underneath (lead-in as the lid goes down), steeper on top (holds).
module snap_skirt(yr) {
    x0 = wall_t + snap_gap;
    translate([0, yr[1], 0]) rotate([90, 0, 0]) linear_extrude(height = yr[1] - yr[0])
        polygon([[x0, lid_z + 0.01], [x0, snap_zb + 0.4], [x0 - snap_bump, snap_zb], [x0, snap_zb - 1.0],
                 [x0, lid_z - snap_h], [x0 + snap_t, lid_z - snap_h], [x0 + snap_t, lid_z + 0.01]]);
}
module snap_groove_cut(yr) {
    translate([wall_t - snap_groove, yr[0] - 0.4, snap_zb - 1.2]) cube([snap_groove + 0.01, yr[1] - yr[0] + 0.8, 1.8]);
}

// The lid in its installed position (tray coordinates).
module lid_installed() {
    translate([wall_t - 1 + lid_clr, pocket_end_y, lid_z])
        cube([inner_w + 2 - 2 * lid_clr, outer_l - far_wall + 1 - pocket_end_y - lid_clr, lid_t]);
    snap_skirt(snap_paddle);
    translate([outer_w, 0, 0]) mirror([1, 0, 0]) snap_skirt(snap_key);
}

// For printing: flipped, top face on the bed, skirts pointing up.
module lid() {
    // a ROTATION (flip over), not a mirror: a mirror would put each tab on the wrong wall
    translate([0, outer_l, outer_h]) rotate([180, 0, 0]) lid_installed();
}

if (part == "tray" || part == "both") tray();
if (part == "lid"  || part == "both") translate([outer_w + 6, 0, 0]) lid();
if (part == "assembled") { tray(); color("orange") lid_installed(); }

echo(str("Wall at the jacks ", wall_t, " mm; Outside ", outer_w, " x ", outer_l, " x ", outer_h, " mm; step (Atom underside) at z = ", atom_bot_z,
         "; jack centres at z = ", jack_z, "; plug hole top at z = ", jack_z + jack_hole_d / 2, " (lid rebate at ", outer_h - lid_t, ")"));

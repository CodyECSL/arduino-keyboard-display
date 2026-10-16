# Game - Crossy Parkway page: design

Date: 2026-10-11. Branch: `CodyECSL/crossy-road-clone`.

## Goal

A new page, "Game - Crossy Parkway" (first called "Game - Crossy", title "BIRD HOP"), between "Game - LabEscape" (the existing jetpack game) and Settings. It is an isometric, landscape hopping game in the style of Crossy Road: hop forward over grass, road, river and rail lanes, collect coins. The mechanics and look follow the genre. All art is original and drawn by code. The name and characters are placeholders, not copies of the commercial game.

## Page order

Media (0), Apps (1), AI (2), Game - LabEscape (3), Game - Crossy Parkway (4), Settings (5). Every game page is named "Game - <Name>" in docs and UI text; identifiers keep their current names. The Settings sidebar entry "Game" becomes "Games" (the longer "Game - LabEscape" text did not fit); its subpage labels the LabEscape score "LabEscape best". `src/ui.c` gets a new tile, the Settings index moves to 5, and `game_set_visible`-style hooks are added for the new page. The page locks page swipes during a run, as the Game page does.

## Files

| File | Purpose |
|---|---|
| `src/crossy_logic.c/.h` | Rules, lane generator, collisions. No LVGL. Host-testable. |
| `src/crossy.c/.h` | LVGL view, touch input, states (title, play, dead), HUD. |
| `src/crossy_assets.c/.h` | Generated sprites. Do not edit. |
| `tools/crossy_art.py` | Small voxel model and isometric renderer. Models for every sprite. |
| `tools/make_crossy_assets.py` | Bakes sprites into C arrays. Also writes a contact-sheet PNG. |
| `tests/host/test_crossy_logic.c` | Host tests. Added to `tests/host/run.sh`. |
| `src/settings.c/.h` | New NVS keys for best distance and total coins. |

## View

- 2:1 isometric. Ground diamond is 32 x 16 px. Forward is up and right on screen; sideways is down and right. A lane is a diagonal band 11 cells wide.
- The world (ground and every sprite) is drawn by a hand-written scanline renderer (`src/crossy_ground.c/.h`) inside one custom draw callback of one LVGL object, straight into the draw buffer. There are no band images, no per-sprite LVGL objects and no `lv_obj_move_to_index`. Each frame the view builds a list of sprites; the renderer paints the ground spans and blits the sprites with a colour key (keyed blit). Grass alternates light and dark cells.
- Per-lane draw order, lanes far to near: layer 0 (lily pads and logs), layer 1 (animals, coins, props, hero), layer 2 (vehicles, train, signals); inside a layer by footprint centre x. Nothing is drawn over the field; the eagle sprites are kept for a later feature. Only the HUD, title and game-over panel are LVGL objects.
- The camera follows the hero and never scrolls back. It does not creep: waiting never kills the hero.
- Hop: about 130 ms, small arc, squash on landing.
- Speed rules from Game - LabEscape: no background of its own and the screen background fill off while the page is up; sprite data copied to PSRAM once at page creation; no alpha overlays or opacity styles; image source swapped only when it changes; no partial refresh. The firmware-wide measures (240 MHz, -O2, pointer-walk rotation, two PSRAM buffers with a flush task) already apply.

## Rules

- Input: one button. A tap hops forward. When a tree, rock or bush blocks the way ahead, the same tap hops diagonally forward to the next lane, around the obstacle (toward the middle of the field if both sides are free, alternating at the middle; a sideways step if both diagonals are blocked). One tap can wait while a hop is going on. No swipes.
- Grass: trees, rocks and bushes block. Coins sit only on cells where taps lead the bird (see `cl_place_coins`): they are placed up to 7 rows ahead of the bird by replaying the tap rules, and none are placed beyond a log lane, where the bird's column depends on the player. Coins never appear on a river lane.
- Road: 2 to 4 vehicles per lane over a 38-cell loop, each lane with its own direction and speed. A hit flattens the hero.
- River: logs of 3 sizes and lily pads. The hero rides a log and drifts with it. Water, or being carried off the field, is a splash death.
- Rail: the signal blinks for about 1 s, then a fast train passes. A hit flattens the hero.
- No eagle: nothing takes a hero that waits (for traffic, a train or a log). Removed in issue #21 together with the camera creep and the behind-the-screen rule, which fired after about 3 to 8 s of waiting. The eagle sprites stay in `crossy_assets` for a later feature.
- Score: furthest lane reached. Lanes come from a seeded generator (same seed, same run). Difficulty rises slowly with distance.
- Title: a demo scrolls behind the title with no autopilot (the view moves the camera over generated lanes). Tap starts a run.
- Dead: a panel shows score, best and coins. Taps are ignored for 600 ms.

## Assets (all original)

- Hero: blocky chicken-like bird; idle and hop frames (the view always uses the forward-facing sprite); flat and drowned poses.
- Scenery: pine, round tree, tall tree, rock, bush, stump, lily pad, 3 log sizes.
- Vehicles (both headings): sedan, taxi, sports car, pickup, box truck, bus.
- Rail: locomotive, carriage, signal with off and on lights.
- Ambient animals: sheep, pig, cow; stand on grass lanes, no collision, 2-frame idle. (Assumption, open to change.)
- Eagle (flying) and a 4-frame coin.

## Size and speed

- Target 400 KB or less of new assets, using 16-bit sprites with a colour key. The assets outgrew that, so the app slot is 3 MB via `partitions_macropad.csv`. The chip has 16 MB and NVS stays at 0x9000.
- Target at least 20 updates/s on hardware and within 20 % of Game - LabEscape's measured 25/s, measured the same way (see `macropad-display-limits` memory).

## Checks

1. Host tests for the rules (`tests/host/run.sh`).
2. Contact-sheet PNG of all sprites, reviewed before flashing.
3. Build with PlatformIO; check the size report.
4. User flashes (hold "USB connected" for 3 s) and checks: tap control, all four lane types, persistence. Frame rate measured with a temporary on-screen table.

## Out of scope for v1

Character unlocks and skins, sound, multiplayer, any change to the Game page.

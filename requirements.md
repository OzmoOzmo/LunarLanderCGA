# Moonlander for DOS: Requirements

## 1. Goal and deliverables

Port the Lunar Lander game to an IBM PC-compatible DOS executable that runs in DOSBox. Preserve the recognizable lander artwork and the Moonlander presentation in `Requirments.jpeg`, while adapting the layout and colors to PC graphics. Build and test in three phases: (1) draw and manually rotate the lander, (2) add thrust and flight controls without gravity or terrain collision, and (3) add terrain, physics, landing, results, telemetry, and scoring. Each phase must produce a runnable DOS program before work on the next phase begins.

## 2. DOS target and display modes

- Deliver `LLander.EXE` (a `.COM` is acceptable if the selected memory model and toolchain support it). It must launch from a DOSBox DOS prompt without Windows runtime dependencies. Prefer C with a DOS-capable compiler such as Open Watcom; use assembly only where a measured bottleneck warrants it. Supply repeatable build instructions and a build script when implementation starts.
- Offer a startup graphics menu, navigable by keyboard, for CGA 320x200 4-color (BIOS mode 04h), VGA 320x200 256-color (mode 13h), and SVGA 800x600 256-color (VESA VBE mode 103h). Do not silently select an unsupported mode: detect support before switching and return to the menu with a readable error if the mode set fails. Always restore the original video mode on exit.
- Support DOSBox with CGA and VGA emulation for their respective modes and a VBE-capable SVGA configuration (for example, `svga_s3`) for SVGA. Document the DOSBox configuration and compiler commands alongside the implementation. No requirement depends on a physical CRT or exact real-hardware timing.
- Maintain one logical 320x200 scene for simulation, sprite placement, HUD boundaries, terrain, and collision. In SVGA, render it at 2x to a centered 640x400 area, with 80-pixel left/right and 100-pixel top/bottom margins. Use mode-specific palettes and drawing backends, but identical game rules and controls. Legibility and contrast take precedence over exact source colors in CGA's four-color palette.
- Keep the title, telemetry, controls, lander, stars, moon, terrain, pad, and flag readable and non-overlapping at all three modes. Do not reproduce the Spectrum's attribute-cell restrictions, 48K label, display-memory layout, ROM printing, TAP format, or Fuse workflow.

## 3. Shared input, state, and rendering rules

- `Z` rotates anticlockwise, `X` rotates clockwise, held `SPACE` fires thrust when enabled, `P` toggles pause, and `Q` quits the current flight to the ready/menu screen. Use a clearly labeled key on the ready screen to start a flight, and a fresh `SPACE` press after a result to start the next round. In Phase 1, `Z` and `X` work immediately; no automatic spin is required.
- Poll held keys without blocking the animation or physics loop. Releasing a key stops its effect; simultaneous rotation and thrust must work in the same update. Treat pause and menu actions as one event per key press, not as repeated toggles on a held key.
- Keep game state (position, velocity, angle, fuel, score, elapsed time, and title/ready/flying/paused/landed/crashed state) separate from mode-specific display code. Use a fixed simulation timestep with frame pacing independent of DOSBox cycles; target smooth 50 updates per second when the emulator can sustain it. Do not change physics speed when rendering takes longer.
- Use integer or fixed-point subpixel coordinates (`FIXED_SCALE = 16` is the initial target). Keep all physical and collision calculations in logical scene coordinates. Put gravity, thrust, rotation, fuel, speed, collision margin, and landing limits together as named constants.
- No visible flicker, rectangular flashes, trails, or background corruption during rotation or flight. Render a complete scene into an offscreen image where feasible and present at a retrace boundary, or use a saved-background/dirty-region approach with synchronized updates where full buffering or page flipping is unavailable. Choose a working strategy for each mode, including CGA and banked VBE SVGA; never erase a moving sprite directly on the visible screen with an exposed blank frame. Avoid full-screen clears during animation. Skip unchanged sprite and HUD regions. Rebuild the static scene only at a screen transition or new round.
- Restore stars, terrain, pad, flag, and HUD wherever the moving lander previously covered them. Clip every draw to the playfield and video memory bounds. Measure rendering in DOSBox and adjust the mode-specific presentation if the target rate is not sustainable; keep motion and input consistent even on slower configurations.

## Phase 1: Lander and manual rotation

### Requirements

- Show the mode menu, a ready/play screen, a fixed starfield with both dim and bright stars, a moon accent, and a centered lander. The background stays static during rotation. Reserve room for the later HUD, controls, and terrain; show the relevant Phase 1 controls on screen.
- Use `99.png` as the flame-on artwork reference and `98.png` as the flame-off artwork reference. The source sheets have seven 8-pixel-cell-aligned angles: 0, 15, 30, 45, 60, 75, and 90 degrees. For `99.png`, crops in cell coordinates are `(0,0)-(2,5)`, `(3,0)-(6,5)`, `(7,0)-(10,5)`, `(11,0)-(15,4)`, `(16,0)-(20,3)`, `(21,0)-(26,3)`, `(27,0)-(32,2)`. For `98.png`, crops are `(0,0)-(2,2)`, `(3,0)-(6,3)`, `(7,0)-(10,3)`, `(11,0)-(14,3)`, `(16,0)-(19,3)`, `(21,0)-(24,3)`, `(27,0)-(29,2)`.
- Keep the center pixel of the original `(1,1)` cell (local pixel `(12,12)`) fixed at the same logical screen location for every crop and quadrant. Convert both sheets ahead of time into 28 ready-to-draw frames per sheet (seven acute angles in each of four mirrored quadrants). Keep each frame's actual bounds and transparency mask; do not give smaller flame-off images flame-on bounds. Choose the nearest 15-degree frame for the current angle. No pixel mirroring, bit reversal, or temporary frame construction in the gameplay loop.
- Phase 1 starts at 0 degrees. Held `Z`/`X` rotate by `ROTATION_STEP = 5` degrees per simulation update; angles wrap across 0/360. Opposing rotation keys cancel. `SPACE` does nothing yet. Keep the lander stationary, visible, and within the playfield throughout a full revolution.

### Acceptance

- Build produces a DOS executable that launches from DOSBox. Each supported graphics mode can be chosen and exited cleanly; unsupported modes are reported without hanging or leaving the display unusable.
- At 0 degrees the artwork matches the source crop and shares the same pivot as the other orientations. Holding `Z` or `X` rotates continuously in the correct direction and stops on release. All four quadrants show the intended precomputed frames.
- In CGA, VGA, and SVGA, repeated rotation leaves the stars and reserved HUD region intact. At normal and deliberately slower DOSBox cycle settings, no visible flashing, full rectangular erasure, or sprite trails occur; controls remain responsive.

## Phase 2: Thrust and controls

### Requirements

- Retain Phase 1's selectable modes and rendering. Add held `SPACE` thrust: use the corresponding `99.png` flame-on frame while fuel is available and the key is down; use the corresponding `98.png` flame-off frame immediately when the key is released or fuel reaches zero. Switch between precomputed frames, not hand-drawn flame pixels.
- Rotation still uses `Z`/`X`; `P` pauses and resumes without advancing time or fuel; `Q` returns to the ready/menu screen. Poll controls in the same simulation update so rotation and thrust can occur together.
- Thrust points along the lander's angle: angle 0 accelerates upward and a small positive clockwise angle accelerates to the right on screen. Consume fuel only while thrust actually fires; fuel never becomes negative. Start with `STARTING_FUEL = 1000` and `THRUST_FUEL_RATE = 2` units per simulation update.
- Keep gravity, terrain collision, landing, and crash disabled in this phase. The lander may remain centered or move in a bounded test area; if it moves, clamp it inside the playfield. Display enough status to verify angle, thrust, and fuel without requiring the final HUD.

### Acceptance

- Held rotation and thrust act simultaneously; key release stops their effects without an input stall. Flame graphics switch cleanly in every quadrant, with no remnants when thrust stops.
- Fuel drops at the defined rate only when firing, reaches exactly zero without underflow, and prevents further thrust. Pause preserves position, angle, and fuel; quit returns to the ready/menu screen.
- All three modes stay free of flicker and sprite trails when the flame changes or the lander moves.

## Phase 3: Terrain, flight, and complete presentation

### Terrain and physics

- Generate one deterministic, bounded piecewise-linear terrain profile per round, with recognizable mountains and valleys, a visible high-contrast surface and stippled fill, and at least one broad, level, marked landing pad. Keep the terrain unchanged during the round; accept a fixed seed for reproducible tests and allow new seeds for subsequent rounds.
- In logical 320x200 coordinates, keep terrain heights within `y = 152..184`, fill no lower than `y = 189`, and clip the last segment to `x = 319`. Reserve a level eight-cell pad at logical cell columns 16..23, `y = 176`, with controlled shoulders at columns 15 and 24, `y = 180`. Bound slopes and verify pad width, levelness, spawn clearance, and an unobstructed approach; regenerate invalid profiles. Mark the pad clearly and draw a flag without obscuring the collision area.
- Use fixed-point positions/velocities with `FIXED_SCALE = 16`; start with `GRAVITY_ACCEL = 1` downward, `THRUST_ACCEL = 4` along the heading, and the Phase 2 fuel constants. Define and enforce maximum speeds, position bounds, and collision margins to avoid overflow, tunneling, or leaving the playfield.
- Apply gravity each active simulation update and thrust only while held with fuel. Track altitude, horizontal and vertical speed, angle, thrust, fuel, and elapsed time. Contact must use the solid ship/landing-leg footprint, not its center or its flame; account for quadrant-specific sprite bounds and the rotation pivot. Ignore flame pixels in collisions.
- A successful landing requires both legs over the pad, no terrain intersection, an angle within `MAX_LANDING_ANGLE = 10` degrees of upright, and horizontal/vertical speed each no more than `MAX_LANDING_SPEED = 12` tenths of a logical pixel per update. Every other terrain contact is a crash. Calibrate the stated physics and landing thresholds together so at least one reproducible landing is achievable.
- Hold a stable `SUCCESS` or `CRASH` state until a fresh restart press. On success, show a small animated flag beside the ship, aligned to the solid feet/pad. On crash, remove the normal ship/flame and show only a flame-free wreck strip; restore any background or text it covered. On restart, reset position, velocity, fuel, angle, timer, and round state, then redraw the new scene once.

### HUD and presentation

- Preserve the reference's `MOONLANDER` title, starfield, moon, left-side `TIME`, `ALT`, `H.SPD`, `V.SPD`, `FUEL`, right-side `THRUST`, `ANGLE`, `SCORE`, and bottom control legend. Adapt placement for the PC scene; omit Spectrum branding. A small rainbow accent is welcome if it remains legible in CGA's limited palette.
- Show `Z - LEFT`, `X - RIGHT`, `SPACE - THRUST`, `P - PAUSE`, and `Q - QUIT` as the actual controls. Show readable ready, paused, landed, crashed, and out-of-fuel status without clearing active gameplay unnecessarily.
- Derive altitude from local terrain height and the lander's solid-leg bottom. Display signed horizontal and vertical speeds in tenths of a logical pixel per update (`SPEED_DISPLAY_SCALE = 10`), including subpixel values and explicit signs. Derive `THRUST` from actual fuel-burning state and `TIME` from simulation updates, not render calls.
- Update only changed HUD fields, using fixed-width text regions that completely replace old values without touching adjacent labels or sprites. Document a score calculation based on landing quality, remaining fuel, and time; score a landing once, not every result frame.

### Acceptance

- Identical seeds reproduce identical terrain; generated terrain stays within bounds and passes slope, pad, and approach checks. A known seed allows a safe landing; the same setup with excessive speed or unsafe angle crashes.
- Gravity moves an unpowered lander downward. Angle 0 thrusts upward; a positive 10-degree angle adds rightward motion. Fuel use and the flame stop together at zero. The ship cannot pass through terrain or leave the play area.
- A safe landing shows `SUCCESS` and its flag; unsafe contact shows `CRASH` without a live thrust flame. Releasing and pressing `SPACE` begins a clean new round without old result text.
- HUD values fit and overwrite previous values at their maximum widths. No lander movement, terrain restoration, or result transition damages static scenery or text. A complete flight from ready screen to result and restart remains responsive and visibly flicker-free in CGA, VGA, and SVGA under DOSBox.

## Implementation and verification workflow

- Keep input, fixed-timestep simulation, terrain/collision, artwork data, and mode-specific drawing/presentation as separate responsibilities. Conversion tools may generate C or assembly data from the provided images at build time; the DOS game must not depend on Python, Pillow, or image files at runtime.
- For each phase, record the DOS compiler command and generated executable, run it in DOSBox in all three supported modes, and verify the phase-specific acceptance criteria. Include a repeatable deterministic terrain/physics test path by Phase 3. If a mode is unsupported in the current DOSBox configuration, report that separately rather than claiming it was tested.
- Defer sound/music, multiplayer, persistent high scores, and elaborate explosions until the three phases are stable.

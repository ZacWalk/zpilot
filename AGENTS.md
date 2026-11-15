# AGENTS.md — working on zpilot

Read [docs/design.md](docs/design.md) first. It records the player's mental model
and the invariants that keep it intact. **Most bad changes to this project are
not bugs — they are violations of that document.** If a change conflicts with
it, the change is wrong unless the human explicitly says the model has moved.
[README.md](README.md) covers the game, its controls and its save file.

---

## Build and verify

```powershell
.\dd.ps1 build debug            # or release; `build` alone builds both
.\dd.ps1 test --label headless  # the no-visible-UI gate (zpilot remains an alias)
.\dd.ps1 clean --dry-run
```

dd sets up the MSVC environment itself; do not try to source `vcvars` by hand.
Build output lands in `build/release` and `build/debug`; checked captures go
under each tree's `captures/run-*/shots`, without deleting earlier runs.

`dd.ps1` and `.dd/` are the [dd](https://github.com/ZacWalk/dd) driver, vendored
unmodified with its hashes in [docs/dd-upstream.json](docs/dd-upstream.json);
never edit them. Keep zpilot's dd configuration in `dd.psd1` and the libdesktop
and libfiles pins in `CMakeLists.txt`. dd's own documentation covers its
[CLI/MCP modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes)
and [built-in commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands).

**`.\dd.ps1 test --label headless` is the gate; run it after every change.** It
runs the unit checks — profiles, input and audio conversion, in
[tests/smoke.cpp](tests/smoke.cpp) and [tests/input.cpp](tests/input.cpp) —
and `zpilot.exe --capture-headless`, the full deterministic fly-through of 1,701 frames
defined by `kScript`, `kTaps` and `kShots` in [src/Main.cpp](src/Main.cpp).
[tests/capture.ps1](tests/capture.ps1) then checks every PNG is non-blank and
was taken on the screen (`Mode`) its `kShots` entry names. The capture draws
every frame in the shell's offscreen window — a real native window, shown so it
paints but cloaked from the desktop and never activated — and each shot again on
libdesktop's software renderer. It takes 10–20 seconds per configuration on a
desktop; a runner without a GPU draws the window on a software adapter, which is
slower, so the check allows ten minutes before calling the run hung. The capture
prints what a window frame costs to draw, mean and peak, and what a shot costs.
Passing cases and actual counts are printed to the console. After a build,
`ctest --preset debug -L headless -V --no-tests=error` reruns without rebuilding
(`release` for Release).
`-capture` remains the explicitly visible interactive variant; do not use it
for unattended verification.

The capture is in three acts. The third is the only way the results screen,
`Grade`, `Profile::Submit` and the save file are ever executed: it relaunches a
planet and holds the drive so the ship leaves without the orb, which ends the
run. Do not delete it to save four seconds. The capture keeps that save file in
its fresh run's `shots/` (`Profile::UseFolder`); the check fails if it
is missing or if either of the player's profiles in `%APPDATA%` changed.

Three traps:

- The frame check reads its shot list from `kShots`, so adding or renaming a
  shot needs no second edit — but a shot whose frame the script never reaches
  fails the check as missing.
- A key the script presses is ignored if the screen is not ready for it (the
  results screen takes none for its first half second), and the game then sits
  on the wrong screen with every frame still lit. That is what the `Mode` (and,
  after the second act's taps, the planet) on each shot catches: the capture
  exits with 100 plus the shot's index. The second act's star-map moves are
  `kTaps`, real key messages that go down and up inside one frame, so they also
  cover the shell's latch for taps shorter than a step; the input test covers
  the same `ui::key_input` path directly.
- The capture is the gameplay coverage. A frame going blank, a shot on the
  wrong screen or the run hanging is a real regression, not a flaky test.

---

## What this project is

A Windows / C++20 game, drawn in 2D in a 16-bit home computer's style, with
**no assets of any kind**: when something looks flat or repetitive, write a
better algorithm, do not add a file
([design §10](docs/design.md#10-everything-is-generated)).
`Application` is a libdesktop `ui::shell`: the shell owns the window, the fixed
60 Hz step, the keys, fullscreen and the loop (bar the capture's, which steps
frame by frame), and the game draws on its canvas through libdesktop's
`render::draw_context` — Direct3D 11 in the window, the software renderer in
screenshots. libdesktop also supplies audio, and
libfiles the PNG encoder ([design §12](docs/design.md#12-platform-and-renderer-boundary)).

---

## Layout

| File | Responsibility |
|---|---|
| `Common.h` | Vec2/3/4, quaternions, `Rng`, value noise and `fbm`. Header only. |
| `Field.*` | Procedural planet: signed field, archetypes, validation, blueprint. |
| `Physics.*` | 2D rigid body solver. Knows about `Field`, knows nothing about the game. |
| `Geometry.*` | Everything visible, generated: the planet slice (rock, bore bands, facets, boundary, sky) built once per planet, and the made and natural figures, the ship and the orb. |
| `Renderer.*` | The 2D camera and drawing primitives over `render::draw_context`; the twelve-bit palette; PNG screenshots. |
| `Font.*` | Stroke font as bars and quads, screen space and world space; the chrome lettering. |
| `Input.*` | The game's keys, bound over the shell's `ui::key_input`. |
| `Synth.*` | Procedural audio rendered into libdesktop's audio stream. |
| `Profile.*` | Save file in `%APPDATA%\zpilot\profile.dat` (the capture keeps its own in `shots/`). |
| `Game.*` | Everything else: modes, flight, props, HUD, star map. `Step` simulates; `Draw` only draws. |
| `Main.cpp` | The `ui::shell` application, fullscreen and screenshots, and the deterministic capture. |

Dependencies run one way: `Main` → `Game` and `Input`; `Game` → everything;
`Geometry` → `Field` and `Renderer`; `Physics` and `Profile` → `Field`; `Font` →
`Renderer`. Nothing calls back into `Game`.

---

## The things that will bite you

### `World::AddBody` can reallocate

`bodies_` is a `std::vector`. **Never hold a `Body&` across a call that might add
one** — including `LatchOrb`. Hold an index instead. Bodies are never erased, but
`RemoveBody` marks one dead and frees its slot for the next `AddBody`, so drop an
index when its body is removed: a stale one can name a newer body.

### Explosions must not recurse

Destroying a tank calls `Blast`, so `Blast` never destroys a tank: it sets the
tank's `Prop::fuse`, and `UpdateProps` does the detonating. Break that and a
chain reaction becomes unbounded recursion.

### `kGenVersion`

In [src/Field.h](src/Field.h). Bump it whenever generation or placement changes.
Saved scores refer to caverns built by a specific generator, and `Profile`
discards the file when the version moves. `Profile`'s own `kFileVersion` is
separate and tracks the *record layout*.

### The slice shares its crossings

The rock, each of the bore's bands and the boundary are cut from the same grid by
the same marching squares (`BuildTerrain`), and they meet with no seam only
because a point where the field crosses a cell edge has one name and one
position whichever cell or strip asks for it (`Grid`), and a saddle cell is
split the same way for all of them. Compute a crossing any other way and
slivers of background open along every wall.

### The slice must stay convex

`draw_polygon` cuts a concave outline into triangles every time it is drawn —
around a hundred microseconds for forty points on Direct3D — and the slice is
drawn every frame. So `TraceStrips` only ever emits convex pieces (cells joined
along a row while the union stays convex, whole-cell runs as rectangles), and
the boundary is drawn as border-only outlines, which are never triangulated.
New terrain geometry must keep to the same rules or the frame time goes.

### Big gradients are slow in software

Shots are drawn on libdesktop's software renderer, where a gradient fill costs
about 140 ns a pixel, a translucent fill about 40 and an opaque rectangle about
one: a single full-screen radial is some 190 ms. Keep large areas opaque and
flat — the nebulae are opaque bands for that reason — and cut gradients down to
where they show, as the bore's lamp pools are cut to the lamp's reach.

### `Draw` must not change the game

`Game::Draw` runs once per paint, not per step, and again for every screenshot.
It reads the state the last step left and nothing else may depend on it: the
lamps are gathered there because they are a drawing concern, and the camera
shake is rolled in `Step` because it comes from `shakeRng_`. Anything that feeds
the simulation or an `Rng` belongs in `Step`, or the capture stops being
deterministic. `GatherLights` sorts the lamps brightest first; the facets take
the first `kMaxLights` and the bore pools only the first `kBorePools`.

### Determinism

The physics runs on a fixed 1/120 step in a fixed order, and generation is a pure
function of the seed. Both are load-bearing: a planet index must always produce
the same cavern. Do not introduce iteration over unordered containers, or
anything that depends on frame rate, in either path.

---

## Conventions

- **British spelling** in comments and identifiers (`colour`, `centre`,
  `normalise`), matching what is already there.
- Comments say **why**, never what. If a comment restates the next line, delete
  it. Most functions need none.
- Player-facing strings are **upper case** — the stroke font has no lower case.
- Tuning constants live at the top of their file with a one-line reason. Prefer
  changing a named constant to adding a special case.
- No `printf` debugging left behind; the only console output is the capture's.
- `.clang-format` is the layout: tabs, Allman braces, 120 columns.
  `.\dd.ps1 fmt` applies it to `src` and `tests` (dd formats only `include`,
  `src` and `tests`, which here is all of zpilot's C++), given clang-format on
  PATH, the only place dd looks; Visual Studio installs one in
  `VC\Tools\Llvm\x64\bin`. Hand-laid tables — the capture script, the
  archetype and sound parameters, the song — keep their layout between
  `// clang-format off` and `// clang-format on`.
- Both targets build at `/W4` with no warnings in Debug and Release, and
  `.clang-tidy` names the bug-finding and performance checks the code is kept
  clean against; a deliberate exception is a
  `// NOLINTNEXTLINE(check): reason` above its line.

---

## Reviewing your own work

Before saying you are done:

1. `.\dd.ps1 test --label zpilot` passes.
2. Nothing you added is set but never read. Dead fields are this codebase's
   recurring fault — check.
3. Player-facing names are consistent. The factory is not sometimes a reactor;
   the orb is not sometimes a pod.
4. If you touched anything visual, re-read the invariants in `docs/design.md`.
5. If you could not see the result, **say so**. Frame mean/peak from the capture
   tells you a frame is not blank; it does not tell you it looks right.

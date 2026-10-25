# AGENTS.md — working on zpilot

Read [docs/design.md](docs/design.md) first. It records the player's mental model
and nine invariants. **Most bad changes to this project are not bugs — they are
violations of that document.** If a change conflicts with it, the change is
wrong unless the human explicitly says the model has moved.

---

## Build and verify

```powershell
.\dd.ps1 build                  # configure + build both configs
.\dd.ps1 launch                 # build Release, then start the game
.\dd.ps1 test --label zpilot   # build both, run the scripted capture, verify the frames
.\dd.ps1 clean --dry-run
```

`.\dd.ps1 build debug` or `release` builds one configuration. dd sets up the
MSVC environment itself; do not try to source `vcvars` by hand.

The repository carries an unmodified [dd](https://github.com/ZacWalk/dd)
checkout as `dd.ps1` plus `.dd/`; [docs/dd-upstream.json](docs/dd-upstream.json)
records the vendored hashes. Keep zpilot's dd configuration in `dd.psd1`, keep
CMake dependency pins in `cmake/dd-dependencies.json`, and use
`.\dd.ps1 commands --json` when you need the project-command list. dd's own
documentation covers its [CLI/MCP modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes)
and [built-in commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands).

**`.\dd.ps1 test --label zpilot` is the gate.** It runs `zpilot.exe -capture`,
a deterministic 1700-frame fly-through defined by `kScript` and `kShots` in
[src/Main.cpp](src/Main.cpp), then [tests/capture.ps1](tests/capture.ps1) checks
every PNG is non-blank and was taken on the screen (`Mode`) its `kShots` entry
names. The capture opens the game's window and takes about half a minute per
configuration — two to four minutes on a CI runner, which has no GPU and renders
in software, so the check allows ten before calling the run hung. Run it after
every change.

The capture is in three acts. The third one exists because it is the only way
the results screen, `Grade`, `Profile::Submit` and the save file are ever
executed: it relaunches a planet and holds the drive so the ship leaves without
the orb, which ends the run. Do not delete it to save four seconds. The capture
keeps that save file in `shots/`, started empty each run
(`Profile::UseFolder`); the check fails if it is missing or if the player's
`%APPDATA%\zpilot\profile.dat` or the legacy `%APPDATA%\z-pilot\profile.dat` changed.
The unit test checks legacy-profile import, new-profile precedence and isolated
saves. Loading never moves a profile; only a later save writes the new folder.

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
  cover `Window`'s latch for taps shorter than a frame.
- The capture is the gameplay coverage. A frame going blank, a
  shot on the wrong screen or the run hanging is a real regression, not a flaky
  test.

Build output lands in `build/release` and `build/debug`, screenshots in the
`shots` folder under each.

---

## What this project is

A Windows / Direct3D 11 / C++17 game with **no assets of any kind**. Rock, ships,
props, glyphs, music and sound effects are all generated in code from a seed.
That is a hard constraint and the point of the exercise: when something looks
flat or repetitive, write a better algorithm, do not add a file.

---

## Layout

| File | Responsibility |
|---|---|
| `Common.h` | Vec2/3/4, Mat4, `Rng`, value noise and `fbm`. Header only. |
| `Field.*` | Procedural planet: signed field, archetypes, validation, blueprint. |
| `Physics.*` | 2D rigid body solver. Knows about `Field`, knows nothing about the game. |
| `Geometry.*` | Every mesh in the game, built from the primitives at the top of the file. |
| `Renderer.*` / `Shaders.h` | HDR scene, additive effects, bloom, CRT composite. |
| `Font.*` | Stroke font, screen space and world space. |
| `Synth.*` | Procedural audio. |
| `Profile.*` | Save file in `%APPDATA%\zpilot\profile.dat` (the capture keeps its own in `shots/`). |
| `Game.*` | Everything else: modes, flight, props, HUD, star map. |

Dependencies run one way: `Game` → everything, `Geometry` → `Field`,
`Physics` → `Field`. Nothing calls back into `Game`.

---

## The things that will bite you

### `World::AddBody` can reallocate

`bodies_` is a `std::vector`. **Never hold a `Body&` across a call that might add
one** — including `LatchOrb`. Bodies are never erased, only marked dead, so a
stored index stays valid; a reference does not.

### Explosions must not recurse

`Blast` marks tanks by setting `Prop::fuse`; it never calls `DestroyProp`.
`UpdateProps` does the detonating. Break that and a chain reaction becomes
unbounded recursion.

### `kGenVersion`

In [src/Field.h](src/Field.h). Bump it whenever generation or placement changes.
Saved scores refer to caverns built by a specific generator, and `Profile`
discards the file when the version moves. `Profile`'s own `kFileVersion` is
separate and tracks the *record layout*.

### The two sheets must agree

`BuildTerrain` (cut face) and `BuildCave` (bore) are built over the same grid and
share the cavern isoline exactly, via `min(±At, AtBase)` as the ring field. They
meet with no seam only because both read the same `Profile::surfaceZ`. Change one
side's height function and they come apart.

### The two constant buffers are memcpy'd, not reflected

`Renderer::FrameCBData` and `ObjectCB` are copied byte for byte into the
cbuffers declared at the top of [src/Shaders.h](src/Shaders.h). Add a field to
one and you must add it in the same position in the other, or every shader
silently reads the wrong slot.

Lights live in `FrameCBData`, so `Renderer::AddLight` has to be called *before*
`BeginScene`, which consumes the set and clears it. `Game::GatherLights` is the
only caller.

### Winding

Back-face culling is on, clockwise-front. A triangle is visible when its
geometric normal points toward the camera, i.e. **-Z**. For a counter-clockwise
2D polygon that means emitting `(p0, p2, p1)`. Get it backwards and the geometry
silently vanishes.

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
- No `printf` debugging left behind; there is no console.

---

## Reviewing your own work

Before saying you are done:

1. `.\dd.ps1 test --label zpilot` passes.
2. Nothing you added is set but never read. This codebase has accumulated
   dead fields more than once — check.
3. Player-facing names are consistent. The factory is not sometimes a reactor;
   the orb is not sometimes a pod.
4. If you touched anything visual, re-read the invariants in `docs/design.md`.
5. If you could not see the result, **say so**. Frame mean/peak from the capture
   tells you a frame is not blank; it does not tell you it looks right.

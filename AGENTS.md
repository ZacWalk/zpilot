# AGENTS.md — working on z-pilot

Read [docs/design.md](docs/design.md) first. It records the player's mental model
and nine invariants. **Most bad changes to this project are not bugs — they are
violations of that document.** If a change conflicts with it, the change is
wrong unless the human explicitly says the model has moved.

---

## Build and verify

```powershell
.\dd.ps1 build          # configure + build both configs
.\dd.ps1 run            # build, then launch
.\dd.ps1 test           # build, run the scripted capture, verify 15 frames
.\dd.ps1 clean
```

`-Config Debug` for a debug build. The script sets up the MSVC environment
itself; do not try to source `vcvars` by hand.

**`.\dd.ps1 test` is the gate.** It runs `z-pilot.exe -capture`, a deterministic
1700-frame fly-through defined by `kScript` and `kShots` in
[src/Main.cpp](src/Main.cpp), then checks every PNG is non-blank. It takes about
eleven seconds. Run it after every change.

The capture is in three acts. The third one exists because it is the only way
the results screen, `Grade`, `Profile::Submit` and the save file are ever
executed: it relaunches a planet and holds the drive so the ship leaves without
the orb, which ends the run. Do not delete it to save four seconds.

Two traps:

- The **shot names are duplicated** in `src/Main.cpp` and in the `$expected`
  array in `dd.ps1`. Change them together or the check fails on a missing file.
- The capture is the only automated coverage there is. A frame going blank or
  the run hanging is a real regression, not a flaky test.

Build output lands in `build/Release`, screenshots in `build/Release/shots`.

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
| `Profile.*` | Save file in `%APPDATA%\z-pilot\profile.dat`. |
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

1. `.\dd.ps1 test` passes.
2. Nothing you added is set but never read. This codebase has accumulated
   dead fields more than once — check.
3. Player-facing names are consistent. The factory is not sometimes a reactor;
   the orb is not sometimes a pod.
4. If you touched anything visual, re-read the invariants in `docs/design.md`.
5. If you could not see the result, **say so**. Frame mean/peak from the capture
   tells you a frame is not blank; it does not tell you it looks right.

## dd build system

This repo uses the vendored dd build system. dd has two modes.

**CLI mode** is the default; each verb runs once and exits:

```pwsh
.\dd.ps1 test                  # build both configs and run the suite
.\dd.ps1 build debug
.\dd.ps1 doctor --json
.\dd.ps1 commands --json       # list this project's own commands
```

**MCP mode** — `.\dd.ps1 mcp` turns the process into a stdio JSON-RPC server for an
MCP client, adapting typed requests onto CLI mode. It owns stdout for protocol
messages, so it prints no result envelope and rejects `--json`. Register it with
`.\dd.ps1 ide --mcp`.

Project settings live in `dd.psd1`; dependency pins live in
`cmake/dd-dependencies.json` with `dependencies.owner = 'dd'`. Vendored dd file
hashes are recorded in `docs/dd-upstream.json`.

# z-pilot

Starting around 1995 I wanted to make a game inspired by the classic games Thrust
and also XPilot. Originally I actually wrote it for DOS using Turbo C++, but it
slowly evolved into what you see here. I pulled this code from a backup CD from
around 2001 and modernised it to build.

---

## The game

You fly a small rocket through tunnels bored into a planet. Someone has cut the
planet in half and you are looking at the exposed face.

Gravity pulls, the rock does not move, and the only thing you have to spend is
fuel. Fuel feeds the engines that move you and the shield capacitor that pays for
your mistakes, so every planet comes down to a single question: how much of your
fuel are you willing to spend on being clumsy?

- **Haul the orb.** It hangs below the ship on a chain that drapes, swings and
  whips. The swing is the whole reason for carrying it.
- **Nothing counts down.** There is no timer and being fast is worth nothing.
  The score is a live percentage — objectives banked, fuel left, shield
  integrity, relics found.
- **One ship per planet, and retry costs one keypress.** A planet you failed is
  a planet you are already flying again.
- **35 planets in a 7×5 grid**, each a fixed seed, each with a name. A row is a
  star system; the next one unlocks once you have cleared four of seven.
- **Everything in the cavern is a rigid body** — the ship, the orb, the chain,
  drums, crates, boulders and propped shelves. Every hazard is therefore also a
  tool, if you are standing in the right place when it goes off.

## No assets

There are no textures, no models, no sound files and no fonts. The rock, the
ships, the props, the glyphs, the music and the effects are all generated in code
from a seed. That is a hard constraint and the point of the exercise: when
something looks flat, the answer is a better algorithm, not another file.

The design notes in [docs/design.md](docs/design.md) describe what the player is
meant to believe is happening, and the invariants the code must not break.

## Controls

| Key | Action |
|---|---|
| `W` / `Up` | Main drive |
| `S` / `Down` | Retro |
| `A` `D` / `Left` `Right` | Turn |
| `Space` | Fire |
| `E` / `Shift` | Tractor beam |
| `Q` / `Ctrl` | Overcharge the shield |
| `F` / `Tab` | Slow time |
| `Enter` | Start / confirm |
| `R` | Restart the planet |
| `P` | Pause |
| `Esc` | Back / quit |
| `F1` | Help |
| `F3` | Mute |
| `F11` | Fullscreen |
| `F12` | Screenshot |

## Building

Windows, Direct3D 11, C++17. You need Visual Studio (MSVC), CMake and Ninja; the
build script sets up the MSVC environment itself.

```powershell
.\dd.ps1 build          # configure and build (Release by default)
.\dd.ps1 run            # build, then launch
.\dd.ps1 test           # build, run the scripted capture, verify the frames
.\dd.ps1 clean
```

Add `-Config Debug` for a debug build. The executable lands in `build/Release`.

`.\dd.ps1 test` runs `z-pilot.exe -capture`, a deterministic fly-through that
drives the game from the keyboard script in [src/Main.cpp](src/Main.cpp) and
writes a set of screenshots, then checks none of them came out blank. It is the
only automated coverage there is.

## Layout

| File | Responsibility |
|---|---|
| `Common.h` | Vector and matrix types, RNG, value noise and fbm |
| `Field.*` | Procedural planet: signed field, archetypes, validation, blueprint |
| `Physics.*` | 2D rigid body solver |
| `Geometry.*` | Every mesh in the game |
| `Renderer.*` / `Shaders.h` | HDR scene, additive effects, bloom, CRT composite |
| `Font.*` | Stroke font, screen space and world space |
| `Synth.*` | Procedural audio |
| `Profile.*` | Save file in `%APPDATA%\z-pilot\profile.dat` |
| `Game.*` | Modes, flight, props, HUD, star map |

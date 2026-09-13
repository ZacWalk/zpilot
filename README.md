# z-pilot

[![Build](https://github.com/ZacWalk/zpilot/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/zpilot/actions/workflows/build.yml)

A gravity-and-thrust cave flyer for Windows. You fly a rocket through tunnels
bored into a planet that has been cut in half, hauling an orb on a chain that
drapes, swings and whips.

Starting around 1995 I wanted to make a game inspired by the classic games Thrust
and also XPilot. Originally I actually wrote it for DOS using Turbo C++, but it
slowly evolved into what you see here. I pulled this code from a backup CD from
around 2001 and modernised it to build.

## The game

Gravity pulls, the rock does not move, and the only thing you spend is fuel —
for the engines that move you and the shield that pays for your mistakes.

- **Nothing counts down.** No timer; speed is worth nothing. The score is a live
  percentage of objectives banked, fuel left, shield integrity and relics found.
- **One ship per planet**, and retrying costs a single keypress.
- **35 planets in a 7×5 grid**, each a fixed seed. A row is a star system; the
  next unlocks once you clear four of seven.
- **Everything is a rigid body** — ship, orb, chain, drums, crates, boulders and
  propped shelves — so every hazard is also a tool.

## No assets

There are no textures, models, sound files or fonts. The rock, ships, props,
glyphs, music and effects are all generated in code from a seed. That is the
point of the exercise: when something looks flat, the answer is a better
algorithm, not another file.

## Controls

<kbd>W</kbd>/<kbd>S</kbd> drive and retro, <kbd>A</kbd>/<kbd>D</kbd> turn,
<kbd>Space</kbd> fires, <kbd>E</kbd> is the tractor beam, <kbd>Q</kbd>
overcharges the shield and <kbd>F</kbd> slows time. <kbd>R</kbd> restarts the
planet, <kbd>F1</kbd> shows help.

## Building

Windows, Direct3D 11, C++17. Requires Visual Studio with the Desktop C++
workload; the vendored [dd](https://github.com/ZacWalk/dd) runtime locates it and
uses the CMake and Ninja that ship with it.

```powershell
.\dd.ps1 build        # both configurations
.\dd.ps1 test         # build and run the suite
.\dd.ps1 run          # build, then launch
```

## Documentation

- [docs/design.md](docs/design.md) — what the player is meant to believe is
  happening, and the invariants the code must not break
- [AGENTS.md](AGENTS.md) — conventions for contributors and coding agents

## License

[MIT](LICENSE)

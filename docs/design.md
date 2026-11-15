# zpilot — the player's mental model

This document is not a feature list. It records **what the player believes is
happening** while they play, and the rules the code must never break in order to
keep that belief intact. When a change would violate something here, the change
is wrong, not the document.

---

## 1. The core fiction

> *You are flying a small rocket through tunnels bored into a planet. Someone has
> cut the planet in half and you are looking at the exposed face.*

Everything else follows from that sentence.

The player is not looking at a side-scrolling level. They are looking at a
**cross section of a solid object**. That is why the rock has strata, why the
tunnels have a visible back wall, and why the caverns get further away as they
get wider.

---

## 2. The camera contract

The camera looks **straight at the cut face**. It never rotates. It only pans,
and zooms by moving nearer or further away. The screen *is* the play plane:
one world unit is the same number of pixels everywhere on it.

The picture is drawn flat, back to front, and depth is something it *says*
rather than something a renderer computes:

| Layer | What it says |
|---|---|
| Space | Stars and nebulae far behind, sliding by slower the further back they are. |
| The atmosphere | The planet's own sky, in bands of height. It moves with the slice, not the stars. |
| The skirt | The planet's top, seen in perspective behind its front edge. |
| The cut face | Rock, dead flat. |
| The bore | The inside of the tunnel, stepping down in bands of colour as it gets deeper. |
| The boundary | The collision line, over both. |
| The play plane | Ship, orb, rope, bullets, particles, every prop. |
| The HUD | Over everything, in screen space. |

**Nothing on the play plane slides against the rock.** Props, the ship and the
cut face move together exactly, because they are one plane; the only parallax
in the frame is between the slice and what lies behind it. A prop given parallax
of its own reads as floating in front of the rock rather than bolted to it.
Particles alone drift a little either side of the plane, so a cloud of them has
some body; they are effects, not things the player has to hit or miss.

**The rule that matters:** *gameplay is 2D and lives on one plane; depth is
scenery.* The player must never have to judge depth to play. Two consequences
that are easy to get wrong:

- **Nothing gameplay-relevant is drawn as if it were behind the plane.** A fuel
  tank drawn small, dimmed into the bore's colour or sliding with the stars
  reads as "sitting on the back wall", which breaks the cross-section: in a
  cut-away, objects are *in* the passage, not pasted onto the surface behind it.
- **Bullets and props must visually agree.** Both are on the play plane, so a
  shot that looks like it hits is a shot that hits.

---

## 3. The cave is a bore, not a valley

The cavern's back surface is the inside of a **deformed half pipe**.

Take the tunnel to be a cylinder of radius `R` whose axis lies in the cut plane.
Slice it lengthwise and you look into a trough: steep at the wall, flat along the
centre line and deepest there. In code that is

```
depth(d) = d · (2R − d) / R
```

where `d` is the distance to the nearest wall and `R` is the radius of the
passage the point is standing in. It is a parabola rather than the true half
circle, `sqrt(d · (2R − d))`: the circle leaves the wall vertically, which would
crowd every band of depth into the wall line, and the parabola leaves it steeply
but at a slope, so the first bands have room to be seen. Both are `R` deep at
the centre line.

This is why the shape is sophisticated rather than just noisy:

- `d` is the signed field, which the game already has.
- `R` is **not** the same thing. It is the radius of the largest inscribed
  circle that still contains the point — a tight neck has a small `R` even at
  its centre, a big chamber has a large `R` right up to its walls. It is
  computed once per planet by propagating `(radius, remaining reach)` outward
  from every cell, which costs three forward-and-back passes over the grid.

The result the player should read:

- **Sides delineated.** The bands crowd together at the wall, so the wall is a
  wall.
- **Curving out at the bottom.** They spread along the centre line, so the
  passage has a floor and not a crease.
- **Narrow tunnels are shallow, big chambers are deep.** Depth tracks width, the
  way a real bore does: a neck reaches two or three bands, a big chamber all
  seven.

Both the bore radius and the wall distance are pushed around by noise, so no two
stretches of tunnel have the same section.

### Stepped, not shaded

The bore is drawn as **seven nested bands**, each the part of the cave deeper
than one step (`kBoreLevel`), each a flat colour: a copper list run down the
inside of the tunnel. Deeper bands are darker and a little more saturated, and
lower in the planet the whole bore drifts toward violet, so every height has its
own palette line. A band is the region where `min(depth − step, open)` is
positive, so each one is cut from inside the last, cell by cell, and they nest
without a gap however the noise falls.

### One grid, two materials

The rock and every band of the bore are cut from **the same grid by the same
marching squares**, and every point where the field crosses a cell edge has one
name and one position whichever piece asks for it. So the rock, the bands and
the boundary meet exactly — no seam, no overlap, no sliver of background.

| | The cut face | The bore |
|---|---|---|
| What it is | Solid rock you are looking at end-on | The inside of the tunnel |
| Drawn as | Strata in four stones, broken into flat facets | Nested bands, one flat colour per step of depth |
| Colour | Dull earth tones, darker toward the core | The star system's own hue |
| Light | None until a lamp reaches it | Lamps pool across it |
| Reads as | Dead stone | Somewhere you can fly |

The cut face is deliberately inert. It is the thing the caverns have to stand
out *from* — if the rock is bright too, the player has nothing to read. A darker
lip runs along every cut, where the facets stop short of the edge, like the
sawn face of a block.

Islands and pillars are simply places where the field never went negative, so
the bore never reached them. They connect to the surrounding rock because they
were never separate from it.

The only hard silhouette in the whole scene is the **planet against the sky**,
which is why that one edge still gets a skirt — a slice of a planet should not
look like paper. Cavern edges never get one.

---

## 4. The boundary line is a gameplay contract

The player flies against a **2D isoline**. That line is the entire collision
model. Every crash, every near miss, every threaded gap is judged against it.

So the line is **drawn explicitly**, as an outline of constant width centred on
the isoline — `kEdgeWidth` world units, zooming with the plane like everything
on it. It is not the edge of a fill, not a gradient, not a by-product of how the
rock happens to be cut — because all of those thin out or thicken with the zoom
and the pieces around them, and the player would be judging their collisions
against something that lies to them.

It is drawn after the rock, the bore and the lamps, so no light ever tints or
dims it. Only a dark planet's torch hides it, along with everything else beyond
its reach.

> **If you can see the bright line, you know exactly where you will crash.**

This is non-negotiable. Any visual change that softens, hides, or varies the
width of that line is a bug.

---

## 5. Everything is a rigid body

The world is simulated, not scripted. There is a purpose-built 2D solver in
`Physics.*` and everything in the cavern lives in it: the ship, the orb, the rope
between them, drums, crates, boulders, shelves and debris.

It is custom because the game needs exactly four things, and none of the usual
extras:

- **Circles and convex polygons**, against each other and against the signed
  field. Terrain never needs colliders — a body asks the field how deep it is and
  which way is out.
- **Breakable joints.** A joint with an impulse threshold is the entire
  structural vocabulary: a drum standing on two legs, a boulder pinned to a
  ceiling, a shelf hinged at the wall and propped at the far end. Cut the weak
  link and the thing moves.
- **One blast call.** `ApplyBlast` shoves every body in range with a smooth
  falloff and a line-of-sight test against the rock, so a charge round a corner
  shakes the walls and nothing else. Chain reactions are not a feature; they are
  what happens when one blast lights another drum's fuse.
- **One impact number.** Every body reports the largest contact impulse it took
  that step. Shield cost, damage, dust and audio all read that single value, so
  hitting rock, a crate and a falling boulder need no special cases.

What that buys, and the reason it is worth the code: **every hazard is also a
tool.** A drum is a threat, a demolition charge, a way to clear a turret nest and
a launch pad, depending on where you are standing when it goes. A player who
works that out has found something the designer never placed.

The orb hangs on a **short chain of bodies**, not one constraint. A single
distance joint is a stick. A chain drapes, swings and whips — and that swing is
the whole reason for hauling the thing.

---

## 6. Objects belong to the walls, not to the floor

Everything the generator places — turrets, the factory, fuel tanks, relics, the
orb's cradle — is **bolted to the 2D cave boundary** and rotated to face out of
it along the wall normal.

Not "standing on a flat shelf". Not "floating in the passage". Not "stuck to the
surface behind". They are attached to the wall, whichever wall is nearest, the
same way real equipment in a mine is bolted to the rock beside you.

Two things this rules out:

- **No artificial flat pads.** Cutting a level ledge into the rock so a box has
  somewhere to stand is the flat way of thinking. The box goes on the wall
  as it is.
- **No depth struts.** A strut running back from a prop to the wall behind
  implies the prop is in front of a surface rather than inside a passage. It
  breaks the cross-section.

The one exception is weight: the orb and the factory are heavy, so they are
placed on a floor-facing wall. Gravity should still make sense.

---

## 7. Fuel is the only clock

There is **no timer**. Nothing counts down, nothing on the HUD measures elapsed
time, and being fast is worth nothing.

The player's single resource is fuel, and it feeds two competing consumers:

- the **engines**, which move you
- the **shield capacitor**, which pays for your mistakes

So every planet reduces to one question: *how much of my fuel am I willing to
spend on being clumsy?* That is the game.

The score is a **percentage, shown live** — objectives banked, fuel left, field
integrity, relics. It is the same rubric the results screen grades, so the
number on the HUD is a running answer to "what do I get if I leave now". Points
would say nothing; a percentage that visibly drains as you burn fuel and take
hits says everything.

The shield follows from the same idea:

- It is **automatic** and almost invisible until it does something.
- It pushes you off walls with an **inverse-square** force, so the last couple of
  metres are a cushion you can feel rather than a cliff you fall off.
- It **deflects** incoming fire rather than swallowing it. A field that absorbs
  bullets is just a hit-point pool with a blue tint; one that bounces them back
  out — still lethal, able to kill whatever fired them — is a field. A wall
  takes a **bite** out of it instead and knocks it offline for a couple of
  seconds, the tensest moment in the game and one the player generated
  themselves.
- You only die when a hit lands with the field already flat.

---

## 8. One ship, and an instant retry

One life per planet. The retry loop *is* the star map.

Because of that, **time-to-retry is the real currency**, not lives. `R` restarts
instantly, with no confirmation, no reload and no fade. A planet you failed is
a planet you are already flying again.

---

## 9. Planets are places, not levels

35 planets in a 7×5 grid, each one a fixed seed. A planet index maps to exactly
one cavern, forever — which is why the card on the star map can show the real
thing, generated from the same seed in a coarser pass.

- Planets have **names**, not numbers. Named places get remembered.
- Each **row is a star system**. Difficulty climbs across a row, the seventh
  planet of each is the hard one, and the next system unlocks at four of seven
  cleared — loose enough that one nasty planet is never a wall.
- Generation is **repair, not rejection**: if the objective cannot be flown to,
  the passage is widened and re-blueprinted, up to three times. The seed never
  changes, so the card always shows the planet you will actually fly.
- `kGenVersion` guards all of this. Change how planets are built and the saved
  scores are discarded, because they referred to caverns that no longer exist.

### Fun is authored, then varied

Noise does not make a level interesting. Each planet picks one of eight
**archetypes** — Descent Shaft, Cavern Chain, Open Silo, Twin Chimney, Long
Gallery, Fortress, Pendulum Run, Powderkeg — and the archetype decides the shape
of the *problem*: how the spine descends, how the passage radius breathes along
it, where the rooms and pillars go, and which props belong there.

Procedural generation should produce **variations on a designed idea**, never the
idea itself. A row mixes archetypes deliberately, because the spread inside a
row matters more than the shape of any single planet.

Every planet also gets **one guaranteed set piece** — a cluster of drums, a
boulder over a choke, a propped shelf with a charge under it — and it is lit to
be noticed. Players remember planets by their one moment; a planet without one
is a corridor.

### Difficulty is space first, then hazards

The cheapest and most honest difficulty dial is **room**. Early caverns are
wide, their chambers scale with the bore, and the archetypes that pinch their
necks barely pinch at all. A generous bore forgives a clumsy approach without
changing a single rule, so nothing has to be explained away.

Hazards are spread out and **arrive weak**. Each one is introduced on its own
planet, several planets apart, and then grows into itself over the next dozen —
a young mine has a small charge, notices you late and gives you longer to leave;
a young drone is slow, short-sighted and does not hit hard; a young singularity
barely pulls. The planet that introduces something is a lesson, not an ambush,
and the same object read at full strength twenty planets later is a genuinely
different problem.

### The retry is the point

A planet is not passed, it is **improved**. You fly it, you get a percentage, and
the card on the star map shows it. Fuel spent, field spent and relics missed are
all visible on the results screen with the weakest line called out, so a retry
always has a target. That is the whole loop.

---

## 10. Everything is generated

No textures, no models, no sprites, no sound files, no fonts. Rock, ships,
props, glyphs, music and effects are all produced in code from a seed.

The desktop icon follows the same rule: Python vector instructions are rasterised
into a build-generated Windows resource, never an imported or runtime-loaded asset.

This is a constraint, and the point of the project. When something looks flat or
repetitive, the answer is a **better algorithm**, not an asset.

Two vocabularies, and they must not blur:

- **Natural** things — the cave walls, boulders, rubble — get irregular,
  faceted outlines in the rock palette. A falling rock is built the way the cave
  is built.
- **Made** things — the ship, drums, crates, turrets, the factory — are allowed
  to be smooth, symmetrical and geometric. That contrast is what tells the player
  at a glance which is which.

The ship is a lathe drawn side on: one hull outline turned from a radius
profile, with three fins grown out of it that swing round and narrow as it rolls
slowly about its long axis. It has no cockpit and no bolted-on parts: one
continuous body that reads as solid rather than as a card.

### The two vocabularies are a material, not just a shape

Made things are **metal**: graded across the part along the key light, with one
bright glint on the edge that faces it and a dark pixel outline, so a prop reads
as one crisp sprite at every zoom. The light stays put as the part turns, which
is what makes it read as lit rather than painted. Natural things never get a
gradient or a glint: they are fans of **flat facets**, each stepped to one of
four tones by which way it faces. No figure gets both. That single split is
what tells the player at a glance which things are equipment and which are the
planet.

### The palette is the era

The look is a 16-bit home computer's, drawn at the screen's own resolution:

- **Twelve-bit colour.** Every solid colour is snapped to four bits a channel,
  the palette registers of the machine the look comes from. Limited steps are
  most of what gives the screen its era.
- **Copper bands.** The sky and the bore change colour in horizontal bands,
  stepping rather than blending, with the next colour dithered in over the last
  lines of each band like a raster split. Where the air thins out it is drawn
  on every second line, then every third and fourth — a screen door — so the
  stars come through it. The title screen's bars are the same trick, moving.
- **Flat clouds.** Nebulae are a few nested bands of palette colour each,
  opaque and never blended, merging where they overlap.
- **Hard edges.** Rock, bands and props are drawn crisp, without anti-aliasing,
  and neighbouring pieces meet with neither gap nor overlap.
- **Glows are for things that emit.** Radial gradients belong to the engine, the
  orb, relics, blasts and lamps — never to stone.

Banding is a choice, and one thing is kept out of it: the boundary contour is
the collision line, so it is never banded, stepped or dithered.

### Nothing in the cavern is lit by a fixture

There are no lamps in the rock. Beyond the fixed key light that shapes made
things, **every light in the frame belongs to something in the fiction** — the
engine, the orb, the factory, a fuel cell, a lit fuse, a blast. So the cavern is
dark until the player does something to it, and an explosion genuinely throws
the walls into relief instead of being a sprite pasted over them: each facet of
the cut face it reaches steps brighter on its own, in hard steps out from the
lamp, and its light pools across the bore. A planet where nothing is
happening is a planet that looks like nothing is happening, and that is the
point.

The budget is small on purpose — the eight strongest lamps light the rock, and
only the four strongest pool across the bore — because a scene lit by
everything at once is lit by nothing.

---

## 11. Invariants

A change that breaks any of these is wrong regardless of how it looks:

1. The camera never rotates; gameplay never requires judging depth.
2. The collision boundary is always drawn as an explicit, constant-width line
   exactly on the isoline, over the rock and the bore and untouched by light.
3. Gameplay objects stay on the play plane and move exactly with the rock; only
   what lies behind the slice has parallax.
4. Placed objects attach to the 2D cave boundary and face out of it.
5. Rock, bore and island are one continuous slice cut from one grid, meeting
   without a seam; the rock is dead stone and only the caves have colour.
6. Nothing in the game counts down, and nothing measures elapsed time.
7. One ship per planet; retry costs one keypress.
8. A planet index always produces the same cavern for a given `kGenVersion`.
9. No external assets, and natural geometry never looks made: facets in the
   rock palette, never a gradient or a highlight.
10. Every light in the world belongs to an object in the fiction; rock takes a
    highlight from nothing.

---

## 12. Platform and renderer boundary

zpilot is a libdesktop `ui::shell` application: one window, no menu bar, and a
`ui::canvas_element` as its content. The shell steps the game at a fixed sixty
steps a second — `Game::Step` is the whole simulation frame — and the canvas's
draw callback is `Game::Draw`, which reads the state the last step left and
changes nothing a step depends on. Keys come from the shell's `key_input`: held,
pressed and released since the last step, so a tap shorter than a step still
lands, and focus loss clears what is held. `Input` maps key codes to the game's
actions; the mouse buttons are two more codes.

Everything visible goes through libdesktop's 2D API. `Renderer` wraps a
`render::draw_context` with the world-to-screen camera, and the whole frame is
crisp polygons, rectangles, lines and gradient fills drawn through it —
Direct3D 11 in the window, the software renderer in a screenshot. zpilot has no
shaders, no meshes and no Direct3D code of its own.

A screenshot, from F12 or the capture, draws the canvas again with
`ui::render_to_surface` at the shot's size. The pixels go to libfiles' PNG
encoder and the finished bytes are published through the shared atomic file
service, so a failed shot never leaves a stale or half written file behind.

`--capture-headless` runs the same application with the shell's offscreen
window: shown, so that it paints, but cloaked from the desktop, never focused
and never brought to the front, which the capture checks before and during the
run. The capture replaces the shell's wall-clock loop with its own — one step
per frame, the script's keys pressed through `key_input`, its taps posted to the
window as real key messages that go down and up inside one frame, then a
present — so the script lands on the same frames however long a frame takes to
draw. The test runner keeps every capture in a fresh directory and verifies the
results act's isolated save file as well as both untouched player profiles.
`-capture` is the visible variant. Only `Main.cpp` calls Windows directly: for
that offscreen check and for the posted taps.

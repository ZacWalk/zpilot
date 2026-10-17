# z-pilot — the player's mental model

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

The camera looks **straight down the Z axis** at a slice through the planet. It
never rotates. It only pans and dollies.

| Depth | What lives there |
|---|---|
| `z ≈ 0` | The play plane. Ship, orb, rope, bullets, particles, every prop. |
| `z ≈ 0…3` | The cut face of the rock — deliberately shallow. |
| `z ≈ 3…55` | The bore: the inside of the tunnel, curving away behind the play plane. |
| `z ≈ 55+` | Nothing. Stars and nebulae live far behind, for parallax only. |

**Every unit of relief in front of the play plane is a bug.** If the cut face
stands proud of the props, the props read as floating in mid-air rather than
bolted to the rock, and the parallax you see as the camera moves is between the
props and the wall instead of between the front of the terrain and the back of
it. The cut face is therefore kept almost flat and all the depth is spent on the
bore behind.

**The rule that matters:** *gameplay is 2D and lives on one plane; depth is
scenery.* The player must never have to judge a Z position to play. Two
consequences that are easy to get wrong:

- **Nothing gameplay-relevant is pushed backwards in Z.** A fuel tank shoved
  10 units behind the play plane reads as "sitting on the back wall", which
  breaks the cross-section: in a cut-away, objects are *in* the passage, not
  pasted onto the surface behind it. They stay at `z ≈ 0`.
- **Bullets and props must visually agree.** Both are on the play plane, so a
  shot that looks like it hits is a shot that hits.

---

## 3. The cave is a bore, not a valley

The cavern's back surface is the inside of a **deformed half pipe**.

Take the tunnel to be a cylinder of radius `R` whose axis lies in the cut plane.
Slice it lengthwise and you see the inside of a half-cylinder: at the wall the
surface is vertical, and at the centre line it is flat and at its deepest. In
code that is exactly

```
depth(d) = sqrt( d · (2R − d) )
```

where `d` is the distance to the nearest wall and `R` is the radius of the
passage the point is standing in.

This is why the shape had to become sophisticated rather than just noisy:

- `d` is the signed field, which the game already has.
- `R` is **not** the same thing. It is the radius of the largest inscribed
  circle that still contains the point — a tight neck has a small `R` even at
  its centre, a big chamber has a large `R` right up to its walls. It is
  computed once per planet by propagating `(radius, remaining reach)` outward
  from every cell, which costs three sweeps of the grid.

The result the player should read:

- **Sides delineated.** The surface leaves the cut edge near-vertically, so the
  wall is a wall.
- **Curving out at the bottom.** It flattens along the centre line, so the
  passage has a floor and not a crease.
- **Narrow tunnels are shallow, big chambers are deep.** Depth tracks width, the
  way a real bore does. This is the whole source of the parallax.

Both the bore radius and the wall distance are pushed around by noise, so no two
stretches of tunnel have the same section.

### Two sheets, two materials

The slice is built as **two meshes over the same grid**, sharing the cavern
isoline exactly so they meet without a seam:

| | The cut face | The bore |
|---|---|---|
| What it is | Solid rock you are looking at end-on | The inside of the tunnel |
| Material | Dull, desaturated, matte | Lit, veined, plasma-washed |
| Emissive | **None** | Full |
| Reads as | Dead stone | Somewhere you can fly |

The cut face is deliberately inert. It is the thing the caverns have to stand
out *from* — if the rock glows too, the player has nothing to read. Because it
has no emissive at all it carries a higher base value than the bore, so it still
looks like stone rather than a hole.

Islands and pillars are simply places where the field never went negative, so
the surface never sank. They connect to the surrounding rock because they were
never separate from it.

The only hard silhouette in the whole scene is the **planet against the sky**,
which is why that one edge still gets a skirt — a slice of a planet should not
look like paper. Cavern edges never get one.

---

## 4. The boundary line is a gameplay contract

The player flies against a **2D isoline**. That line is the entire collision
model. Every crash, every near miss, every threaded gap is judged against it.

So the line is **drawn explicitly**, as a ribbon of constant world width baked
along the isoline. It is not a shading gradient, not a fresnel edge, not a
by-product of the tessellation — because all of those thin out or thicken
depending on viewing angle and mesh density, and the player would be judging
their collisions against something that lies to them.

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
  somewhere to stand is the old, flat way of thinking. The box goes on the wall
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
thing, generated from the same seed at a quarter resolution.

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

No textures, no models, no sound files, no fonts. Rock, ships, props, glyphs,
music and effects are all produced in code from a seed.

This is a constraint, and the point of the project. When something looks flat or
repetitive, the answer is a **better algorithm**, not an asset.

Two vocabularies, and they must not blur:

- **Natural** things — the cave walls, boulders, rubble — get complex, irregular,
  faceted geometry in the rock palette. A falling rock is built the way the cave
  is built.
- **Made** things — the ship, drums, crates, turrets, the factory — are allowed
  to be smooth, symmetrical and geometric. That contrast is what tells the player
  at a glance which is which.

The ship is a single lathed hull with its fins grown out of the same surface by
modulating the radius, and it rolls slowly about its long axis. It has no
cockpit and no bolted-on parts: one continuous body that reads from every angle.

### Nothing in the cavern is lit by a fixture

There are no lamps in the rock. Beyond a fixed key and fill, **every light in
the frame belongs to something in the fiction** — the engine, the orb, the
factory, a fuel cell, a lit fuse, a blast. So the cavern is dark until the
player does something to it, and an explosion genuinely throws the walls into
relief instead of being a sprite pasted over them. A planet where nothing is
happening is a planet that looks like nothing is happening, and that is the
point.

The budget is small on purpose — the eight strongest lamps win — because a
scene lit by everything at once is lit by nothing.

### The two vocabularies are a material, not just a shape

Made things take a **specular highlight**. Natural things never do; they get
procedural **grain** instead, evaluated in world space, which tilts the normal
and mottles the albedo so the enormous low-resolution rock sheet reads as stone
rather than as facets. No mesh gets both. That single split is what tells the
player at a glance which things are equipment and which are the planet.

Natural things are also drawn the way a software renderer of the era drew rock:
the grain and the light are read once per world-space **texel**, on whichever
axis plane faces the surface, and the stone's brightness is stepped into bands
joined by an ordered dither. Crisp and point-sampled, never smeared. The boundary
contour is kept out of that ramp — it is the collision line, so it is never
banded or dithered.

---

## 11. Invariants

A change that breaks any of these is wrong regardless of how it looks:

1. The camera never rotates; gameplay never requires judging depth.
2. The collision boundary is always drawn as an explicit, constant-width line
   that lies *on* the surface, not in front of it.
3. Gameplay objects stay on the play plane, and nothing stands proud of it.
4. Placed objects attach to the 2D cave boundary and face out of it.
5. Rock, bore and island are one continuous surface; only the caves glow.
6. Nothing in the game counts down, and nothing measures elapsed time.
7. One ship per planet; retry costs one keypress.
8. A planet index always produces the same cavern for a given `kGenVersion`.
9. No external assets, and natural geometry never looks made.
10. Every light in the world belongs to an object in the fiction; rock takes a
    highlight from nothing.

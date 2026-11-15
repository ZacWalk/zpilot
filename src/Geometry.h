// zpilot - every visible thing in the game, generated here in code: the
// planet slice from the field, and the made and natural shapes that live on
// the play plane. No sprites, no bitmaps.
#pragma once
#include "Field.h"
#include "Renderer.h"

namespace zp
{

	// The one key light, from the upper left of the play plane. Made things are
	// lit across by it; rock facets turn toward or away from it.
	const Vec2 kLightDir = {-0.6f, 0.8f};

	// How a part of a figure takes the light. Made things are smooth and get a
	// highlight; natural things are faceted and never do.
	enum Shade : uint8_t
	{
		SHADE_FLAT,  // one colour
		SHADE_METAL, // made: graded across by the key light, with a glint
		SHADE_GLOW,  // emits: a hot core out to its colour at the rim
		SHADE_ROCK,  // natural: faceted from its centre, in the planet's stone
	};

	struct FigurePart
	{
		std::vector<Vec2> pts; // local, y up, counter clockwise
		Vec4 colour;
		Shade shade = SHADE_METAL;
		bool accent = false; // takes the instance's live colour: lamps, cores
		bool outline = true; // a dark pixel rim, so it reads against the rock
	};

	// A made or natural thing as polygons in its own frame: the drawing, not the
	// collision shape, which is Physics' own.
	struct Figure
	{
		std::vector<FigurePart> parts;
	};

	// What changes per instance and per frame.
	struct Look
	{
		Vec4 accent = {1, 1, 1, 1};
		float flash = 0; // a hit, toward white
		float alpha = 1;
		Vec4 rock = {0.4f, 0.35f, 0.3f, 1}; // the planet's stone, for natural parts
	};

	void DrawFigure(Renderer& r, const Figure& s, Vec2 pos, float angle, Vec2 scale, const Look& look);

	// The ship: one lathed hull with three fins grown out of it, seen side on.
	// It rolls about its long axis, so the fins swing round and narrow.
	void DrawShip(Renderer& r, Vec2 pos, float angle, float roll, float scale, float heat, const Look& look);
	// The orb: a sphere whose equator band and its marks turn as it spins.
	void DrawOrb(Renderer& r, Vec2 pos, float spin, float glow);

	Figure BuildPedestal();
	Figure BuildFuelCell();
	Figure BuildTurretBase();
	Figure BuildTurretBarrel();
	Figure BuildFactory();
	Figure BuildFactoryCore();
	Figure BuildMine();
	Figure BuildDrone();
	Figure BuildWellRing();
	Figure BuildWellCore();
	Figure BuildRelic();
	// Physics props, built at unit size and scaled per instance, so one figure
	// covers every drum, crate and slab the generator asks for.
	Figure BuildTank();
	Figure BuildCrate();
	Figure BuildBoulder();
	Figure BuildSlab();

	// ---------------------------------------------------------------- planet

	// A convex piece of the slice, in world units.
	struct Piece
	{
		std::vector<Vec2> pts;
		Vec2 lo, hi;
		bool rect = false; // axis aligned, so it is drawn as a plain rectangle
	};

	// The planet slice, built once per planet from its field. The rock and the
	// bore are cut into one strip per grid row, so each strip can take its own
	// colour - strata in the rock, copper bands in the bore - and neighbouring
	// strips share their row line exactly. Every piece is convex: the renderer
	// cuts a concave outline into triangles each time it draws one, and the slice
	// is drawn every frame.
	struct Terrain
	{
		static constexpr int kBoreLevels = 7;

		int rows = 0;
		float cell = 2.0f;
		Vec2 origin;

		std::vector<std::vector<Piece>> rock;              // [row]
		std::vector<std::vector<Piece>> bore[kBoreLevels]; // [level][row]
		std::vector<Vec4> rim;                             // [row] rock seen at the cut edge

		// The cut face, broken into flat-shaded facets in the rock's colours.
		struct Facet
		{
			Vec2 p[4];
			int n = 0;
			Vec4 colour;  // unlit
			Vec2 centre;  // where lamps are measured from
			float lo, hi; // x extent
		};
		float facetPitch = 4.0f;
		std::vector<std::vector<Facet>> facets; // [facet row]

		// The collision boundary, each contour whole and closed, so it is drawn as
		// one outline of constant width, mitred at every corner. A contour that runs
		// off the map is closed by a detour far outside it.
		struct Contour
		{
			std::vector<Vec2> pts;
			Vec2 lo, hi; // of the part on the map
		};
		std::vector<Contour> boundary;
		// The stretches of it along the planet's edge against the sky.
		struct Run
		{
			std::vector<Vec2> pts;
			Vec2 lo, hi;
		};
		std::vector<Run> surface;

		// Far behind the slice, for parallax only.
		struct Star
		{
			Vec2 p;
			float depth = 0, size = 0;
			Vec3 colour;
		};
		std::vector<Star> stars;
		// Gas clouds further back still: a few flat bands of palette colour each,
		// brightest at the core, never blended.
		struct Nebula
		{
			Vec2 p;
			float depth = 0, size = 0;
			Vec3 bands[3];             // outer to core, opaque over space
			Vec2 drift[3];             // each band's centre, off the cloud's, unit size
			std::vector<Vec2> outline; // convex, unit size
		};
		std::vector<Nebula> nebulae;

		// The palette this planet was given.
		Vec3 strata[4]; // the stones its layers are cut from
		Vec3 stone;     // the commonest of them, for boulders and the skirt
		Vec3 boreHue;   // hue, saturation and value of the bore at its wall
		Vec3 horizon;   // the atmosphere's glow along the skyline
		float surfaceY = 0;
	};

	void BuildTerrain(const Field& f, const LevelSpec& spec, Terrain& out);

	// A lamp: something in the fiction giving off light, its intensity folded into
	// its colour.
	struct Lamp
	{
		Vec2 pos;
		float radius = 0;
		Vec3 colour;
	};

	// Per-frame state the slice is drawn with.
	struct TerrainLook
	{
		float time = 0;
		float meltdown = 0; // the factory going: the bore washes red
		Vec4 edge;          // the boundary line
		float edgeWidth = 0.54f;
		std::span<const Lamp> lamps;
	};

	void DrawSky(Renderer& r, const Terrain& t, float time);
	// Lamps light the cut face facet by facet and pool over the bore; the sky
	// takes no light, as nothing is there to catch it.
	void DrawTerrain(Renderer& r, const Terrain& t, const TerrainLook& look);
	// Drawn after the lamps, so no light ever tints or dims the line the player
	// flies against.
	void DrawBoundary(Renderer& r, const Terrain& t, const TerrainLook& look);

} // namespace zp

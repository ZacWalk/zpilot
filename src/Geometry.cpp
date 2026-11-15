#include "Geometry.h"

#include <algorithm>
#include <array>

namespace zp
{

	namespace
	{

		// Outlines are a fixed pixel width, so a prop reads as one crisp sprite at
		// every zoom.
		constexpr float kOutlinePx = 1.5f;
		// A part smaller than this on screen gets no edge highlight: it would be all
		// highlight.
		constexpr float kGlintMinPx = 10.0f;

		Vec2 Light()
		{
			return normalize(kLightDir);
		}

		std::vector<Vec2> RegularPoly(int sides, float radius, float phase = 0.0f)
		{
			std::vector<Vec2> p;
			p.reserve(sides);
			for (int i = 0; i < sides; ++i)
			{
				float a = phase + kTau * i / sides;
				p.push_back({cosf(a) * radius, sinf(a) * radius});
			}
			return p;
		}

		std::vector<Vec2> Box(float x0, float y0, float x1, float y1)
		{
			return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
		}

		std::vector<Vec2> Offset(std::vector<Vec2> pts, Vec2 by)
		{
			for (auto& p : pts) p += by;
			return pts;
		}

		FigurePart Part(std::vector<Vec2> pts, Vec3 colour, Shade shade = SHADE_METAL, bool accent = false,
		                bool outline = true)
		{
			FigurePart p;
			p.pts = std::move(pts);
			p.colour = Vec4(colour, 1.0f);
			p.shade = shade;
			p.accent = accent;
			p.outline = outline;
			return p;
		}

		Vec4 Finish(Vec4 c, const Look& look)
		{
			c = Mix(c, Vec4(1, 1, 1, c.w), look.flash);
			c.w *= look.alpha;
			return c;
		}

		Vec4 Rim(Vec4 c)
		{
			return Vec4(c.x * 0.22f, c.y * 0.22f, c.z * 0.26f, c.w);
		}

		// Made metal: graded across the part along the key light, with one bright edge
		// where the light catches it. The light stays put as the part turns, which is
		// what makes it read as lit rather than painted.
		void DrawMetal(Renderer& r, std::span<const Vec2> w, Vec4 colour, bool outline, const Look& look)
		{
			const Vec2 L = Light();
			Vec2 c(0, 0);
			for (const Vec2& p : w) c += p;
			c = c / (float)w.size();
			float lo = 0, hi = 0;
			for (const Vec2& p : w)
			{
				const float d = dot(p - c, L);
				lo = std::min(lo, d);
				hi = std::max(hi, d);
			}
			const Vec4 lit = Finish(Ocs(colour.x * 1.45f, colour.y * 1.45f, colour.z * 1.45f, colour.w), look);
			const Vec4 dark = Finish(Ocs(colour.x * 0.50f, colour.y * 0.50f, colour.z * 0.55f, colour.w), look);
			const Fill fill = Fill::Linear(c + L * hi, lit, c + L * lo, dark);
			r.Poly(w, fill, outline ? Finish(Rim(colour), look) : Vec4(), outline ? kOutlinePx : 0.0f);

			if ((hi - lo) * r.Scale() < kGlintMinPx) return;
			// The edge whose outward normal looks most into the light.
			float area = 0;
			for (size_t i = 0; i < w.size(); ++i) area += cross(w[i], w[(i + 1) % w.size()]);
			float best = 0.35f;
			int edge = -1;
			for (size_t i = 0; i < w.size(); ++i)
			{
				const Vec2 e = w[(i + 1) % w.size()] - w[i];
				if (lengthSq(e) < 1e-6f) continue;
				Vec2 n = normalize(Vec2(e.y, -e.x));
				if (area < 0) n = -n;
				const float facing = dot(n, L);
				if (facing > best)
				{
					best = facing;
					edge = (int)i;
				}
			}
			if (edge < 0) return;
			const Vec2 a = w[(size_t)edge], b = w[((size_t)edge + 1) % w.size()];
			const Vec4 glint = Finish(Vec4(1.0f, 1.0f, 0.95f, 0.75f * colour.w), look);
			r.Line(lerp(a, b, 0.18f), lerp(a, b, 0.82f), 1.5f, glint);
		}

		void DrawGlowPart(Renderer& r, std::span<const Vec2> w, Vec4 colour, bool outline, const Look& look)
		{
			Vec2 c(0, 0);
			for (const Vec2& p : w) c += p;
			c = c / (float)w.size();
			float reach = 0;
			for (const Vec2& p : w) reach = std::max(reach, length(p - c));
			const Vec4 core = Finish(Mix(colour, Vec4(1, 1, 1, colour.w), 0.65f), look);
			const Vec4 rim = Finish(colour, look);
			r.Poly(w, Fill::Radial(c, reach, core, rim), outline ? Finish(Rim(colour), look) : Vec4(),
			       outline ? kOutlinePx : 0.0f);
		}

		// Natural: a fan of facets from the centre, each turned toward or away from the
		// light by its own edge and stepped to one of four tones. No highlight, ever.
		void DrawRockPart(Renderer& r, std::span<const Vec2> w, Vec4 stone, const Look& look)
		{
			const Vec2 L = Light();
			Vec2 c(0, 0);
			for (const Vec2& p : w) c += p;
			c = c / (float)w.size();
			float area = 0;
			for (size_t i = 0; i < w.size(); ++i) area += cross(w[i], w[(i + 1) % w.size()]);
			for (size_t i = 0; i < w.size(); ++i)
			{
				const Vec2 a = w[i], b = w[(i + 1) % w.size()];
				Vec2 n = normalize(Vec2(b.y - a.y, a.x - b.x));
				if (area < 0) n = -n;
				const float facing = dot(n, L) * 0.5f + 0.5f;
				const float tone = 0.62f + floorf(facing * 3.999f) * 0.16f;
				const Vec2 tri[3] = {c, a, b};
				r.Poly(tri, Fill::Solid(Finish(Ocs(stone.x * tone, stone.y * tone, stone.z * tone), look)));
			}
			r.Poly(w, Fill::Solid({0, 0, 0, 0}), Finish(Ocs(stone.x * 0.35f, stone.y * 0.35f, stone.z * 0.35f), look),
			       kOutlinePx);
		}

	} // namespace

	void DrawFigure(Renderer& r, const Figure& s, Vec2 pos, float angle, Vec2 scale, const Look& look)
	{
		static std::vector<Vec2> w;
		const float c = cosf(angle), sn = sinf(angle);
		for (const FigurePart& part : s.parts)
		{
			w.clear();
			for (const Vec2& p : part.pts)
			{
				const Vec2 q(p.x * scale.x, p.y * scale.y);
				w.push_back({pos.x + q.x * c - q.y * sn, pos.y + q.x * sn + q.y * c});
			}
			const Vec4 colour = part.accent ? look.accent : part.colour;
			switch (part.shade)
			{
			case SHADE_METAL: DrawMetal(r, w, colour, part.outline, look); break;
			case SHADE_GLOW: DrawGlowPart(r, w, colour, part.outline, look); break;
			case SHADE_ROCK: DrawRockPart(r, w, look.rock, look); break;
			default:
				r.Poly(w, Fill::Solid(Finish(colour, look)), part.outline ? Finish(Rim(colour), look) : Vec4(),
				       part.outline ? kOutlinePx : 0.0f);
				break;
			}
		}
	}

	// ----------------------------------------------------------------- ship

	namespace
	{

		// The lathe: radius along the hull's long axis, nose first.
		constexpr float kHull[][2] = {{2.05f, 0.03f},  {1.72f, 0.20f},  {1.30f, 0.36f},  {0.60f, 0.47f},
		                              {-0.10f, 0.50f}, {-0.72f, 0.48f}, {-1.10f, 0.44f}, {-1.42f, 0.38f}};
		constexpr int kHullRings = (int)(sizeof(kHull) / sizeof(kHull[0]));
		constexpr int kHullSamples = 15;

		float HullRadius(float y)
		{
			if (y >= kHull[0][0]) return kHull[0][1];
			for (int i = 0; i + 1 < kHullRings; ++i)
				if (y >= kHull[i + 1][0])
				{
					const float t = (kHull[i][0] - y) / (kHull[i][0] - kHull[i + 1][0]);
					return lerpf(kHull[i][1], kHull[i + 1][1], t);
				}
			return kHull[kHullRings - 1][1];
		}

		// The fins grow out of the hull from its waist to its tail.
		float FinReach(float y)
		{
			return powf(saturatef((0.35f - y) / 1.75f), 1.7f) * 0.92f;
		}

	} // namespace

	void DrawShip(Renderer& r, Vec2 pos, float angle, float roll, float scale, float heat, const Look& look)
	{
		const float c = cosf(angle), s = sinf(angle);
		auto world = [&](float x, float y)
		{
			x *= scale;
			y *= scale;
			return Vec2(pos.x + x * c - y * s, pos.y + x * s + y * c);
		};
		const float top = kHull[0][0], bottom = kHull[kHullRings - 1][0];

		std::array<Vec2, kHullSamples * 2> hull;
		for (int i = 0; i < kHullSamples; ++i)
		{
			const float y = lerpf(top, bottom, (float)i / (kHullSamples - 1));
			const float rad = HullRadius(y);
			hull[(size_t)i] = world(rad, y);
			hull[(size_t)(kHullSamples * 2 - 1 - i)] = world(-rad, y);
		}

		// A fin is a blade at its own angle round the hull. Seen side on it spans
		// its whole reach; turned toward the camera it shrinks onto the hull.
		struct Fin
		{
			float across, toward;
		} fins[3];
		for (int k = 0; k < 3; ++k)
		{
			const float a = roll + kTau * k / 3.0f;
			fins[k] = {cosf(a), sinf(a)};
		}
		const Vec3 finBase(0.30f, 0.48f, 0.86f);
		auto drawFin = [&](const Fin& f)
		{
			if (fabsf(f.across) < 0.06f) return;
			constexpr int n = 7;
			Vec2 blade[n * 2];
			for (int i = 0; i < n; ++i)
			{
				const float y = lerpf(0.35f, bottom, (float)i / (n - 1));
				const float root = HullRadius(y) * f.across;
				const float tip = (HullRadius(y) + FinReach(y)) * f.across;
				blade[i] = world(root, y);
				blade[n * 2 - 1 - i] = world(tip, y);
			}
			// A fin turned toward the camera faces the light more.
			const float tone = 0.75f + 0.45f * saturatef(0.5f - f.toward * 0.5f);
			DrawMetal(r, blade, Vec4(finBase * tone, 1.0f), true, look);
		};
		for (const Fin& f : fins)
			if (f.toward > 0.0f) drawFin(f);
		// A lathe is round: it is lit across its width, brightest down the side that
		// faces the light, with a highlight running its length.
		const Vec2 across(c, s);
		const float side = dot(across, Light()) >= 0.0f ? 1.0f : -1.0f;
		const Vec2 waist = world(0.0f, 0.2f);
		const Vec3 metal(0.62f, 0.74f, 0.90f);
		const Fill sheen = Fill::Linear(waist + across * (0.5f * scale * side), Finish(Ocs(metal * 1.35f), look),
		                                waist - across * (0.5f * scale * side), Finish(Ocs(metal * 0.48f), look));
		r.Poly(hull, sheen, Finish(Rim(Vec4(metal, 1.0f)), look), kOutlinePx);
		r.Line(world(0.22f * side, 1.35f), world(0.30f * side, -0.95f), std::max(1.5f, 0.09f * scale * r.Scale()),
		       Finish(Vec4(1.0f, 1.0f, 1.0f, 0.65f), look));
		for (const Fin& f : fins)
			if (f.toward <= 0.0f) drawFin(f);

		// The engine mouth is the one hot part of the hull.
		const Vec2 mouth = world(0.0f, bottom - 0.04f);
		const float h = saturatef(heat);
		r.Glow(mouth, (0.45f + 0.55f * h) * scale, Finish(Hot(Vec3(1.1f, 1.9f, 2.9f) * (0.6f + h)), look));
		const Vec2 nozzle[4] = {world(-0.30f, bottom + 0.02f), world(0.30f, bottom + 0.02f),
		                        world(0.24f, bottom - 0.10f), world(-0.24f, bottom - 0.10f)};
		r.Poly(nozzle, Fill::Solid(Finish(Hot(Vec3(0.9f, 1.6f, 2.6f) * (0.5f + h)), look)));
	}

	void DrawOrb(Renderer& r, Vec2 pos, float spin, float glow)
	{
		const float g = saturatef(glow);
		const auto shell = RegularPoly(16, 1.30f);
		std::vector<Vec2> w;
		w.reserve(shell.size());
		for (const Vec2& p : shell) w.push_back(pos + p);
		const Vec4 rim = Ocs(0.42f + 0.25f * g, 0.16f, 0.62f + 0.2f * g);
		const Vec4 core = Ocs(0.95f, 0.70f + 0.2f * g, 1.0f);
		r.Poly(w, Fill::Radial(pos + Vec2(-0.35f, 0.40f), 1.6f, core, rim), Rim(rim), kOutlinePx);

		// The equator band, tipped by the spin, with marks that roll round it.
		const float tilt = spin * 0.35f;
		const Vec2 ax = fromAngle(tilt), ay = perp(ax);
		const Vec2 band[4] = {pos + ax * -1.32f + ay * -0.14f, pos + ax * 1.32f + ay * -0.14f,
		                      pos + ax * 1.32f + ay * 0.14f, pos + ax * -1.32f + ay * 0.14f};
		r.Poly(band, Fill::Solid(Ocs(0.55f + 0.3f * g, 0.25f, 0.85f)));
		for (int i = 0; i < 6; ++i)
		{
			const float lon = spin + kTau * i / 6.0f;
			if (cosf(lon) <= 0.15f) continue;
			const Vec2 at = pos + ax * (sinf(lon) * 1.22f);
			const float wide = 0.10f + 0.10f * cosf(lon);
			const Vec2 mark[4] = {at + ax * -wide + ay * -0.12f, at + ax * wide + ay * -0.12f,
			                      at + ax * wide + ay * 0.12f, at + ax * -wide + ay * 0.12f};
			r.Poly(mark, Fill::Solid(Ocs(1.0f, 0.85f, 1.0f)));
		}
	}

	// --------------------------------------------------------------- shapes
	// Made things are geometric, symmetrical and lit with a highlight. They are
	// built here from their outlines, local +y pointing out of the wall.

	Figure BuildPedestal()
	{
		Figure s;
		const Vec3 c(0.24f, 0.27f, 0.38f);
		s.parts.push_back(Part({{-1.70f, -1.60f}, {1.70f, -1.60f}, {1.20f, -0.25f}, {-1.20f, -0.25f}}, c));
		s.parts.push_back(Part({{-1.10f, -0.30f}, {1.10f, -0.30f}, {0.85f, 0.45f}, {-0.85f, 0.45f}}, c * 1.35f));
		return s;
	}

	// A tank box on legs, stencilled FUEL by the game.
	Figure BuildFuelCell()
	{
		Figure s;
		const Vec3 trim(0.12f, 0.30f, 0.24f);
		s.parts.push_back(Part({{-1.02f, -1.35f}, {-0.72f, -1.35f}, {-0.62f, -0.35f}, {-0.92f, -0.35f}}, trim));
		s.parts.push_back(Part({{0.72f, -1.35f}, {1.02f, -1.35f}, {0.92f, -0.35f}, {0.62f, -0.35f}}, trim));
		s.parts.push_back(Part(Box(-1.15f, -0.35f, 1.15f, 1.05f), Vec3(0.18f, 0.46f, 0.34f)));
		s.parts.push_back(Part(Box(-1.30f, 0.98f, 1.30f, 1.20f), Vec3(0.3f, 1.0f, 0.6f), SHADE_FLAT, true));
		return s;
	}

	// A mounting plate sunk into the rock and the socket the barrel swings in.
	Figure BuildTurretBase()
	{
		Figure s;
		const Vec3 c(0.36f, 0.27f, 0.36f);
		s.parts.push_back(Part({{-2.40f, -1.60f}, {2.40f, -1.60f}, {1.85f, 0.35f}, {-1.85f, 0.35f}}, c));
		s.parts.push_back(Part(Offset(RegularPoly(8, 1.10f, kPi / 8.0f), {0, 0.50f}), c * 1.3f));
		s.parts.push_back(Part(Box(-0.30f, 0.20f, 0.30f, 0.70f), Vec3(1, 0.3f, 0.3f), SHADE_GLOW, true));
		return s;
	}

	Figure BuildTurretBarrel()
	{
		Figure s;
		const Vec3 c(0.60f, 0.30f, 0.36f);
		s.parts.push_back(Part({{-0.34f, -0.30f}, {0.34f, -0.30f}, {0.26f, 2.70f}, {-0.26f, 2.70f}}, c));
		s.parts.push_back(Part(Box(-0.44f, 2.45f, 0.44f, 2.85f), c * 1.25f));
		return s;
	}

	// A dome on a base with a chimney, the way the original drew it.
	Figure BuildFactory()
	{
		Figure s;
		const Vec3 c(0.34f, 0.29f, 0.42f);
		s.parts.push_back(Part(Box(0.95f, 1.20f, 2.05f, 4.60f), c * 1.1f));
		s.parts.push_back(Part(Box(0.68f, 4.45f, 2.32f, 5.10f), Vec3(0.62f, 0.42f, 0.30f)));
		s.parts.push_back(Part({{-4.20f, -3.60f}, {4.20f, -3.60f}, {3.10f, -1.70f}, {-3.10f, -1.70f}}, c));
		s.parts.push_back(Part(RegularPoly(10, 2.05f, kPi / 10.0f), c * 0.85f));
		return s;
	}

	Figure BuildFactoryCore()
	{
		Figure s;
		s.parts.push_back(Part(RegularPoly(14, 1.75f), Vec3(1, 0.6f, 0.2f), SHADE_GLOW, true));
		return s;
	}

	// A spiked drum: long spikes so the silhouette says "do not touch", and a hot
	// core the game pulses on the fuse.
	Figure BuildMine()
	{
		Figure s;
		std::vector<Vec2> star;
		const int pts = 8;
		for (int i = 0; i < pts * 2; ++i)
		{
			const float a = kTau * i / (pts * 2) + 0.11f;
			const float rad = (i & 1) ? 0.62f : 1.45f;
			star.push_back({cosf(a) * rad, sinf(a) * rad});
		}
		s.parts.push_back(Part(star, Vec3(0.52f, 0.42f, 0.26f)));
		s.parts.push_back(Part(RegularPoly(6, 0.55f), Vec3(1, 0.7f, 0.2f), SHADE_GLOW, true));
		return s;
	}

	// A forward swept dart: the nose says which way it has committed.
	Figure BuildDrone()
	{
		Figure s;
		// clang-format off
		s.parts.push_back(Part({{0.0f, 1.70f}, {-0.62f, 0.35f}, {-1.45f, -0.75f}, {-0.40f, -0.30f},
		                        {0.0f, -0.85f}, {0.40f, -0.30f}, {1.45f, -0.75f}, {0.62f, 0.35f}},
		                       Vec3(0.72f, 0.22f, 0.28f)));
		// clang-format on
		s.parts.push_back(Part(Box(-0.16f, -0.55f, 0.16f, 0.95f), Vec3(0.28f, 0.10f, 0.14f), SHADE_FLAT));
		s.parts.push_back(Part(RegularPoly(3, 0.36f, kPi * 0.5f), Vec3(1, 0.4f, 0.3f), SHADE_GLOW, true));
		return s;
	}

	Figure BuildWellRing()
	{
		Figure s;
		std::vector<Vec2> ring;
		const int n = 9;
		for (int i = 0; i < n * 2; ++i)
		{
			const float a = kTau * i / (n * 2);
			const float rad = (i & 1) ? 2.15f : 3.40f;
			ring.push_back({cosf(a) * rad, sinf(a) * rad});
		}
		s.parts.push_back(Part(ring, Vec3(0.26f, 0.24f, 0.42f)));
		return s;
	}

	Figure BuildWellCore()
	{
		Figure s;
		s.parts.push_back(Part(RegularPoly(5, 1.30f, 0.31f), Vec3(0.8f, 0.7f, 1.0f), SHADE_GLOW, true));
		return s;
	}

	// A cut gem; the game squeezes it across as it turns, like a spinning coin.
	Figure BuildRelic()
	{
		Figure s;
		s.parts.push_back(Part({{0.0f, 1.0f}, {-0.72f, 0.25f}, {-0.45f, -0.80f}, {0.45f, -0.80f}, {0.72f, 0.25f}},
		                       Vec3(1.0f, 0.78f, 0.25f), SHADE_GLOW, true));
		s.parts.push_back(Part({{0.0f, 1.0f}, {-0.30f, 0.25f}, {0.0f, -0.80f}, {0.30f, 0.25f}}, Vec3(1.0f, 0.95f, 0.7f),
		                       SHADE_FLAT, false, false));
		return s;
	}

	// Volatile things are round and banded; inert things are flat sided. The
	// player has to read which will move and which will go off without a legend.
	Figure BuildTank()
	{
		Figure s;
		s.parts.push_back(Part(RegularPoly(9, 1.0f, 0.15f), Vec3(0.66f, 0.40f, 0.16f)));
		s.parts.push_back(Part(RegularPoly(9, 0.62f, 0.15f), Vec3(0.36f, 0.26f, 0.18f)));
		s.parts.push_back(Part(Box(-1.0f, -0.28f, 1.0f, 0.28f), Vec3(1, 0.55f, 0.1f), SHADE_GLOW, true));
		return s;
	}

	Figure BuildCrate()
	{
		Figure s;
		const Vec3 c(0.32f, 0.37f, 0.48f);
		s.parts.push_back(Part(Box(-1.0f, -1.0f, 1.0f, 1.0f), c));
		s.parts.push_back(Part({{-0.86f, -0.62f}, {-0.62f, -0.86f}, {0.86f, 0.62f}, {0.62f, 0.86f}}, c * 1.5f,
		                       SHADE_FLAT, false, false));
		s.parts.push_back(Part(Box(-1.0f, -0.16f, 1.0f, 0.16f), Vec3(0.55f, 0.75f, 1.0f)));
		return s;
	}

	// Rock, so it gets the cave's own vocabulary: irregular, faceted, in the
	// planet's stone. A falling boulder is built the way the walls are built.
	Figure BuildBoulder()
	{
		Figure s;
		std::vector<Vec2> pts;
		const int seg = 13;
		for (int i = 0; i < seg; ++i)
		{
			const float a = kTau * i / seg;
			const float n = fbm(cosf(a) * 1.9f + 4.0f, sinf(a) * 1.9f, 3, 8123u);
			const float rad = 0.80f + n * 0.42f;
			pts.push_back({cosf(a) * rad, sinf(a) * rad});
		}
		s.parts.push_back(Part(pts, Vec3(1, 1, 1), SHADE_ROCK, false, false));
		return s;
	}

	Figure BuildSlab()
	{
		Figure s;
		s.parts.push_back(Part(Box(-1.0f, -1.0f, 1.0f, 1.0f), Vec3(0.28f, 0.32f, 0.40f)));
		return s;
	}

	// --------------------------------------------------------------- planet

	namespace
	{

		// Depths at which the bore steps down, wall first. Each step is one band of
		// colour; a wide chamber reaches more of them than a tight neck does.
		constexpr float kBoreLevel[Terrain::kBoreLevels] = {0.0f, 1.8f, 4.0f, 6.8f, 10.0f, 14.0f, 19.0f};
		// Facets stop this far inside the cut, in field units, so a darker rim runs
		// along every edge like the lip of a sawn block.
		constexpr float kFacetInset = 0.9f;
		constexpr float kFacetPitch = 5.0f;
		constexpr float kFacetJitter = 1.6f;
		constexpr int kRunSegments = 24;
		// Lamps that pool their light over the bore; each costs a redraw of the cave
		// around it.
		constexpr int kBorePools = 4;
		constexpr int kPoolSides = 16;
		constexpr float kSkirt = 20.0f; // the planet's thickness, seen against the sky
		constexpr int kStarCount = 640;
		constexpr int kNebulaCount = 12;
		const Vec3 kSpace = {0.01f, 0.01f, 0.035f};
		// Each nebula band inside the last, as a share of its size.
		constexpr float kNebulaBand[3] = {1.0f, 0.68f, 0.40f};
		// How far each facet turns from the face, as a share of its stratum's tone. Kept
		// small: the strata are the pattern, the facets only break it up.
		constexpr float kFacetTone[4] = {0.84f, 0.94f, 1.04f, 1.14f};

		// Each star system has its own stone, bore and sky, so a row of planets reads
		// as one place and the next row as somewhere else. Four tones of stone each,
		// one per kind of layer.
		const Vec3 kStrata[5][4] = {
			{{0.36f, 0.27f, 0.20f}, {0.46f, 0.35f, 0.24f}, {0.29f, 0.23f, 0.19f}, {0.53f, 0.42f, 0.30f}},
			{{0.28f, 0.30f, 0.35f}, {0.36f, 0.38f, 0.43f}, {0.23f, 0.25f, 0.30f}, {0.43f, 0.43f, 0.45f}},
			{{0.40f, 0.22f, 0.17f}, {0.50f, 0.30f, 0.22f}, {0.31f, 0.19f, 0.16f}, {0.57f, 0.38f, 0.26f}},
			{{0.32f, 0.32f, 0.22f}, {0.40f, 0.40f, 0.27f}, {0.26f, 0.26f, 0.19f}, {0.47f, 0.45f, 0.30f}},
			{{0.30f, 0.25f, 0.34f}, {0.38f, 0.31f, 0.42f}, {0.24f, 0.20f, 0.28f}, {0.45f, 0.37f, 0.46f}},
		};
		// Hue, saturation and value of the bore at its wall.
		const Vec3 kBore[5] = {{0.555f, 0.72f, 0.66f},
		                       {0.47f, 0.70f, 0.60f},
		                       {0.60f, 0.66f, 0.66f},
		                       {0.51f, 0.62f, 0.60f},
		                       {0.68f, 0.56f, 0.62f}};
		const Vec3 kHorizon[5] = {{0.60f, 0.22f, 0.42f},
		                          {0.20f, 0.42f, 0.50f},
		                          {0.66f, 0.36f, 0.20f},
		                          {0.30f, 0.48f, 0.30f},
		                          {0.48f, 0.24f, 0.60f}};
		// The hue its nebulae are drawn round.
		const float kNebulaHue[5] = {0.80f, 0.58f, 0.97f, 0.47f, 0.72f};

		// A sampled field and the names of its points. A point keeps one name and one
		// position whichever cell asks for it, so pieces built cell by cell and strip
		// by strip meet exactly.
		struct Grid
		{
			const float* v;
			int nx, ny;
			Vec2 origin;
			float cell;

			float At(int x, int y) const { return v[(size_t)y * nx + x]; }
			Vec2 Pos(int x, int y) const { return {origin.x + x * cell, origin.y + y * cell}; }
			Vec2 CrossH(int x, int y) const
			{
				const float a = At(x, y), b = At(x + 1, y);
				return {origin.x + (x + a / (a - b)) * cell, origin.y + y * cell};
			}
			Vec2 CrossV(int x, int y) const
			{
				const float a = At(x, y), b = At(x, y + 1);
				return {origin.x + x * cell, origin.y + (y + a / (a - b)) * cell};
			}
			// Corners, crossings on horizontal edges and crossings on vertical edges.
			int Id(int x, int y, int kind) const { return ((y * nx + x) * 3) + kind; }
		};

		struct GridPt
		{
			int id;
			Vec2 p;
			uint8_t sides; // a bit for each side of the cell the point lies on
		};

		// The part of cell (x, y) where the field is positive, counter clockwise, as one
		// ring - or two, when a saddle's centre is outside and its lobes do not join.
		template <typename F> void CellPieces(const Grid& g, int x, int y, F&& emit)
		{
			const int cx[4] = {x, x + 1, x + 1, x}, cy[4] = {y, y, y + 1, y + 1};
			float v[4];
			bool in[4];
			int count = 0;
			for (int k = 0; k < 4; ++k)
			{
				v[k] = g.At(cx[k], cy[k]);
				in[k] = v[k] > 0.0f;
				count += in[k];
			}
			if (count == 0) return;
			auto corner = [&](int k)
			{
				return GridPt{g.Id(cx[k], cy[k], 0), g.Pos(cx[k], cy[k]), (uint8_t)((1 << k) | (1 << ((k + 3) & 3)))};
			};
			auto cross = [&](int k)
			{
				switch (k)
				{
				case 0: return GridPt{g.Id(x, y, 1), g.CrossH(x, y), 1};
				case 1: return GridPt{g.Id(x + 1, y, 2), g.CrossV(x + 1, y), 2};
				case 2: return GridPt{g.Id(x, y + 1, 1), g.CrossH(x, y + 1), 4};
				default: return GridPt{g.Id(x, y, 2), g.CrossV(x, y), 8};
				}
			};
			const bool saddle = in[0] == in[2] && in[1] == in[3] && in[0] != in[1];
			if (saddle && (v[0] + v[1] + v[2] + v[3]) <= 0.0f)
			{
				for (int k = 0; k < 4; ++k)
				{
					if (!in[k]) continue;
					const GridPt lobe[3] = {corner(k), cross(k), cross((k + 3) & 3)};
					emit(lobe, 3);
				}
				return;
			}
			GridPt ring[8];
			int n = 0;
			for (int k = 0; k < 4; ++k)
			{
				if (in[k]) ring[n++] = corner(k);
				if (in[k] != in[(k + 1) & 3]) ring[n++] = cross(k);
			}
			if (n >= 3) emit(ring, n);
		}

		void Bounds(Piece& p)
		{
			p.lo = p.hi = p.pts.front();
			for (const Vec2& q : p.pts)
			{
				p.lo = {std::min(p.lo.x, q.x), std::min(p.lo.y, q.y)};
				p.hi = {std::max(p.hi.x, q.x), std::max(p.hi.y, q.y)};
			}
			p.rect = p.pts.size() == 4;
			for (size_t i = 0; i < p.pts.size() && p.rect; ++i)
			{
				const Vec2 a = p.pts[i], b = p.pts[(i + 1) % p.pts.size()];
				p.rect = a.x == b.x || a.y == b.y;
			}
		}

		// Where a ring runs along the vertical line x: the index of the first of two
		// consecutive points on it, going up if `up`, or -1.
		int SideEdge(const std::vector<Vec2>& ring, float x, bool up)
		{
			const size_t n = ring.size();
			for (size_t i = 0; i < n; ++i)
			{
				const Vec2 a = ring[i], b = ring[(i + 1) % n];
				if (a.x == x && b.x == x && (up ? a.y < b.y : a.y > b.y)) return (int)i;
			}
			return -1;
		}

		float Turn(Vec2 a, Vec2 b, Vec2 c)
		{
			return cross(b - a, c - b);
		}

		// Joins two convex rings that share a vertical side, `left` running up it and
		// `right` down it, when the result is still convex. Corners the join leaves
		// straight are dropped, so a run of whole cells stays four points.
		bool MergeConvex(const std::vector<Vec2>& left, const std::vector<Vec2>& right, float x, std::vector<Vec2>& out)
		{
			const int i = SideEdge(left, x, true), j = SideEdge(right, x, false);
			if (i < 0 || j < 0) return false;
			const size_t n = left.size(), m = right.size();
			const Vec2 u = left[(size_t)i], v = left[((size_t)i + 1) % n];
			const Vec2 rv = right[(size_t)j], ru = right[((size_t)j + 1) % m];
			if (rv.x != v.x || rv.y != v.y || ru.x != u.x || ru.y != u.y) return false;
			const Vec2 beforeU = left[((size_t)i + n - 1) % n], afterV = left[((size_t)i + 2) % n];
			const Vec2 afterU = right[((size_t)j + 2) % m], beforeV = right[((size_t)j + m - 1) % m];
			const float turnU = Turn(beforeU, u, afterU), turnV = Turn(beforeV, v, afterV);
			const float slack = 1e-6f;
			if (turnU < -slack || turnV < -slack) return false;
			out.clear();
			// From v round the left ring to u, then round the right ring back to v.
			for (size_t k = 0; k < n; ++k)
			{
				if (k == 0 && turnV <= slack) continue;
				if (k == n - 1 && turnU <= slack) continue;
				out.push_back(left[((size_t)i + 1 + k) % n]);
			}
			for (size_t k = 2; k < m; ++k) out.push_back(right[((size_t)j + k) % m]);
			return out.size() >= 3;
		}

		// The positive region of the field, one strip per grid row, as convex pieces.
		// Each cell's own piece is convex; neighbours are joined left to right for as
		// long as the union stays convex, so the renderer never has to cut a concave
		// outline into triangles, and a run of whole cells is one rectangle.
		void TraceStrips(const Grid& g, std::vector<std::vector<Piece>>& rows)
		{
			rows.assign((size_t)g.ny - 1, {});
			std::vector<Vec2> open, merged, ring[2];
			for (int y = 0; y + 1 < g.ny; ++y)
			{
				auto& out = rows[(size_t)y];
				bool hasOpen = false;
				auto emit = [&](std::vector<Vec2>& pts)
				{
					if (pts.size() < 3) return;
					Piece piece;
					piece.pts = pts;
					Bounds(piece);
					out.push_back(std::move(piece));
				};
				for (int x = 0; x + 1 < g.nx; ++x)
				{
					int count = 0;
					CellPieces(g, x, y, [&](const GridPt* pts, int n)
					{
						ring[count].clear();
						for (int i = 0; i < n; ++i) ring[count].push_back(pts[i].p);
						++count;
					});
					const float xl = g.Pos(x, y).x, xr = g.Pos(x + 1, y).x;
					bool joined = false;
					for (int k = 0; k < count; ++k)
						if (SideEdge(ring[k], xl, false) >= 0) joined = true;
					if (hasOpen && !joined)
					{
						emit(open);
						hasOpen = false;
					}
					// The piece on the left side joins first, before one that reaches the
					// right takes its place as the open piece.
					int order[2] = {0, 1};
					if (count == 2 && SideEdge(ring[1], xl, false) >= 0) std::swap(order[0], order[1]);
					for (int o = 0; o < count; ++o)
					{
						const int k = order[o];
						std::vector<Vec2>* cur = &ring[k];
						if (hasOpen && SideEdge(ring[k], xl, false) >= 0)
						{
							if (MergeConvex(open, ring[k], xl, merged)) cur = &merged;
							else emit(open);
							hasOpen = false;
						}
						if (x + 2 < g.nx && SideEdge(*cur, xr, true) >= 0)
						{
							open = *cur;
							hasOpen = true;
						}
						else
						{
							emit(*cur);
						}
					}
				}
				if (hasOpen) emit(open);
			}
		}

		// Cuts a convex polygon down to the part inside a convex, counter clockwise
		// window.
		void ClipToWindow(std::vector<Vec2>& poly, std::span<const Vec2> window, std::vector<Vec2>& scratch)
		{
			for (size_t i = 0; i < window.size() && poly.size() >= 3; ++i)
			{
				const Vec2 a = window[i], e = window[(i + 1) % window.size()] - a;
				scratch.clear();
				for (size_t j = 0; j < poly.size(); ++j)
				{
					const Vec2 p = poly[j], q = poly[(j + 1) % poly.size()];
					const float dp = cross(e, p - a), dq = cross(e, q - a);
					if (dp >= 0.0f) scratch.push_back(p);
					if ((dp >= 0.0f) != (dq >= 0.0f)) scratch.push_back(lerp(p, q, dp / (dp - dq)));
				}
				poly.swap(scratch);
			}
		}

		// The convex hull of a handful of points, counter clockwise.
		std::vector<Vec2> ConvexHull(std::vector<Vec2> pts)
		{
			std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
			std::vector<Vec2> hull(pts.size() * 2);
			size_t k = 0;
			for (size_t pass = 0; pass < 2; ++pass)
			{
				const size_t floor = k;
				for (size_t i = 0; i < pts.size(); ++i)
				{
					const Vec2 p = pass == 0 ? pts[i] : pts[pts.size() - 1 - i];
					while (k >= floor + 2 && cross(hull[k - 1] - hull[k - 2], p - hull[k - 2]) <= 0.0f) --k;
					hull[k++] = p;
				}
				--k;
			}
			hull.resize(k);
			return hull;
		}

		// Bilinear, the way Field::Sample reads its own grid.
		float SampleGrid(const std::vector<float>& v, const Grid& g, Vec2 p)
		{
			const float gx = clampf((p.x - g.origin.x) / g.cell, 0.0f, g.nx - 1.001f);
			const float gy = clampf((p.y - g.origin.y) / g.cell, 0.0f, g.ny - 1.001f);
			const int x0 = (int)gx, y0 = (int)gy;
			const float fx = gx - x0, fy = gy - y0;
			auto at = [&](int x, int y)
			{
				return v[(size_t)y * g.nx + x];
			};
			return lerpf(lerpf(at(x0, y0), at(x0 + 1, y0), fx), lerpf(at(x0, y0 + 1), at(x0 + 1, y0 + 1), fx), fy);
		}

		// The zero contour of the field, rock on its left: whole loops, and the open
		// chains that run off the edge of the map, closed again far outside it so
		// every one can be drawn as an outline. The surface parts are kept as runs for
		// the skirt.
		void TraceBoundary(const Grid& g, const std::vector<float>& base, Terrain& t)
		{
			const size_t ids = (size_t)g.nx * g.ny * 3;
			std::vector<int> next(ids, -1);
			std::vector<char> entered(ids, 0), seen(ids, 0);
			std::vector<Vec2> pos(ids);
			for (int y = 0; y + 1 < g.ny; ++y)
				for (int x = 0; x + 1 < g.nx; ++x)
					CellPieces(g, x, y, [&](const GridPt* pts, int n)
					{
						for (int i = 0; i < n; ++i)
						{
							const GridPt& a = pts[i];
							const GridPt& b = pts[(i + 1) % n];
							if (a.sides & b.sides) continue; // a cell wall, not the contour
							next[(size_t)a.id] = b.id;
							entered[(size_t)b.id] = 1;
							pos[(size_t)a.id] = a.p;
							pos[(size_t)b.id] = b.p;
						}
					});

			const Vec2 lo = g.Pos(0, 0), hi = g.Pos(g.nx - 1, g.ny - 1);
			const float far = 400.0f;
			std::vector<Vec2> chain;
			auto keep = [&](bool closed)
			{
				if (chain.size() < 2) return;
				Terrain::Contour c;
				c.pts = chain;
				if (!closed)
				{
					// Out from the end, round a box well clear of the map, and back in to
					// the start. The camera never leaves the map, so the detour is never
					// seen, and it crosses nothing: the contour is all inside the map.
					auto outward = [&](Vec2 p)
					{
						const float dl = p.x - lo.x, dr = hi.x - p.x, db = p.y - lo.y, dt = hi.y - p.y;
						const float m = std::min(std::min(dl, dr), std::min(db, dt));
						if (m == dl) return Vec2(lo.x - far, p.y);
						if (m == dr) return Vec2(hi.x + far, p.y);
						if (m == db) return Vec2(p.x, lo.y - far);
						return Vec2(p.x, hi.y + far);
					};
					const Vec2 out = outward(chain.back()), back = outward(chain.front());
					c.pts.push_back(out);
					// Round the box the long way, below the map, whichever sides they left by.
					const Vec2 corners[4] = {{hi.x + far, lo.y - far},
					                         {lo.x - far, lo.y - far},
					                         {lo.x - far, hi.y + far},
					                         {hi.x + far, hi.y + far}};
					auto side = [&](Vec2 p)
					{
						return p.x > hi.x ? 0 : p.y < lo.y ? 1 : p.x < lo.x ? 2 : 3;
					};
					for (int s = side(out), guard = 0; s != side(back) && guard < 4; s = (s + 1) & 3, ++guard)
						c.pts.push_back(corners[s]);
					c.pts.push_back(back);
				}
				c.lo = c.hi = chain.front();
				for (const Vec2& p : chain)
				{
					c.lo = {std::min(c.lo.x, p.x), std::min(c.lo.y, p.y)};
					c.hi = {std::max(c.hi.x, p.x), std::max(c.hi.y, p.y)};
				}
				t.boundary.push_back(std::move(c));

				// Runs along the planet's edge against the sky, for its thickness.
				Terrain::Run run;
				auto flush = [&]()
				{
					if (run.pts.size() >= 2) t.surface.push_back(run);
					run.pts.clear();
				};
				for (size_t i = 0; i < chain.size(); ++i)
				{
					const bool onEdge = fabsf(SampleGrid(base, g, chain[i])) < 2.5f;
					if (!onEdge)
					{
						flush();
						continue;
					}
					run.pts.push_back(chain[i]);
					if (run.pts.size() > (size_t)kRunSegments)
					{
						const Vec2 last = run.pts.back();
						flush();
						run.pts.push_back(last);
					}
				}
				flush();
			};
			auto follow = [&](int start)
			{
				chain.clear();
				int at = start;
				while (at >= 0 && !seen[(size_t)at])
				{
					seen[(size_t)at] = 1;
					chain.push_back(pos[(size_t)at]);
					at = next[(size_t)at];
				}
				const bool closed = at == start;
				if (!closed && at >= 0) chain.push_back(pos[(size_t)at]);
				keep(closed);
			};
			// Open contours start at the map's edge; whatever is left is a loop.
			for (size_t i = 0; i < ids; ++i)
				if (next[i] >= 0 && !entered[i] && !seen[i]) follow((int)i);
			for (size_t i = 0; i < ids; ++i)
				if (next[i] >= 0 && !seen[i]) follow((int)i);
			for (Terrain::Run& run : t.surface)
			{
				run.lo = run.hi = run.pts.front();
				for (const Vec2& p : run.pts)
				{
					run.lo = {std::min(run.lo.x, p.x), std::min(run.lo.y, p.y)};
					run.hi = {std::max(run.hi.x, p.x), std::max(run.hi.y, p.y)};
				}
			}
		}

		// Sedimentary layers of uneven thickness, offset across a few faults and
		// darkening toward the core. Each layer is one of the planet's four stones,
		// never the same one twice running.
		Vec3 Strata(const Vec3* tones, float surfaceY, Vec2 p, uint32_t seed)
		{
			const float fault = (hash2((int)floorf(p.x / 58.0f), 3, seed + 5u) - 0.5f) * 7.0f;
			const float y = p.y + fault;
			const float layer = y / 4.2f + fbm(p.x * 0.012f, y * 0.03f, 2, seed + 919u) * 0.9f;
			const int s = (int)floorf(layer);
			auto pick = [&](int at)
			{
				return (int)(hash2(at, 11, seed + 41u) * 3.999f);
			};
			int tone = pick(s);
			if (tone == pick(s - 1)) tone = (tone + 1) & 3;
			const float deep = 0.70f + 0.30f * saturatef(p.y / std::max(1.0f, surfaceY));
			return tones[tone] * deep;
		}

		// Clips a convex polygon to where a field, linear along its edges, is
		// positive.
		int ClipPositive(const Vec2* in, const float* f, int n, Vec2* out)
		{
			int m = 0;
			for (int i = 0; i < n; ++i)
			{
				const int j = (i + 1) % n;
				if (f[i] > 0.0f) out[m++] = in[i];
				if ((f[i] > 0.0f) != (f[j] > 0.0f)) out[m++] = lerp(in[i], in[j], f[i] / (f[i] - f[j]));
			}
			return m;
		}

	} // namespace

	void BuildTerrain(const Field& f, const LevelSpec& spec, Terrain& t)
	{
		t = Terrain();
		const int nx = f.NX(), ny = f.NY();
		const size_t total = (size_t)nx * ny;
		const uint32_t seed = spec.seed;
		const int family = std::clamp(spec.row, 0, 4);
		const float jitter = (hash2(spec.index, 1, 977u) - 0.5f) * 0.12f;
		t.rows = ny - 1;
		t.cell = f.Cell();
		t.origin = f.Min();
		for (int i = 0; i < 4; ++i) t.strata[i] = kStrata[family][i] * (0.94f + jitter);
		t.stone = t.strata[1];
		t.boreHue = kBore[family] + Vec3(jitter * 0.25f, 0, 0);
		t.horizon = kHorizon[family];

		std::vector<float> v(total), base(total), level(total), depth(total);
		float surfaceSum = 0;
		for (int x = 0; x < nx; ++x)
		{
			int top = 0;
			for (int y = 0; y < ny; ++y)
			{
				const size_t i = (size_t)y * nx + x;
				// Exactly zero would sit on the line; nudged into the open, it never does.
				v[i] = f.At(x, y);
				if (v[i] == 0.0f) v[i] = -1e-5f;
				base[i] = f.AtBase(x, y);
				if (base[i] > 0.0f) top = y;
			}
			surfaceSum += f.CellCentre(x, top).y;
		}
		t.surfaceY = surfaceSum / nx;

		const Grid rockGrid{v.data(), nx, ny, t.origin, t.cell};
		TraceStrips(rockGrid, t.rock);
		TraceBoundary(rockGrid, base, t);

		// The bore: d is how far a point is from the nearest wall, R the radius of
		// the passage it is in. A parabola over the passage is steep at the wall
		// and flat along the middle, so a narrow neck stays shallow and a chamber
		// goes deep - the cross section of a tunnel, not of a bowl.
		for (int y = 0; y < ny; ++y)
			for (int x = 0; x < nx; ++x)
			{
				const size_t i = (size_t)y * nx + x;
				depth[i] = -1.0f;
				if (v[i] >= 0.0f || base[i] <= 0.0f) continue;
				const Vec2 p = f.CellCentre(x, y);
				const float d = -v[i];
				const float wob = fbm(p.x * 0.018f, p.y * 0.018f, 3, seed + 613u);
				const float R = std::max(3.5f, std::max(f.RadiusAt(x, y), d) * (1.0f + wob * 0.30f));
				const float dd = std::min(d * (1.0f + fbm(p.x * 0.034f, p.y * 0.034f, 2, seed + 727u) * 0.24f), R);
				const float rough = fbm(p.x * 0.07f, p.y * 0.07f, 2, seed + 211u) * 1.6f * saturatef(d / 6.0f);
				depth[i] = dd * (2.0f * R - dd) / R + rough;
			}
		// Never above the band outside it at any grid point, so each band nests
		// inside the last and the first inside the open cave, cell by cell.
		std::vector<float> open(total);
		for (size_t i = 0; i < total; ++i) open[i] = std::min(-v[i], base[i]);
		for (int k = 0; k < Terrain::kBoreLevels; ++k)
		{
			for (size_t i = 0; i < total; ++i)
				level[i] = k == 0 ? open[i] : std::min(depth[i] - kBoreLevel[k], open[i]);
			for (float& value : level)
				if (value == 0.0f) value = -1e-5f;
			const Grid g{level.data(), nx, ny, t.origin, t.cell};
			TraceStrips(g, t.bore[k]);
		}

		t.rim.resize((size_t)t.rows);
		const float midX = (f.Min().x + f.Max().x) * 0.5f;
		for (int y = 0; y < t.rows; ++y)
		{
			const Vec3 c = Strata(t.strata, t.surfaceY, {midX, t.origin.y + (y + 0.5f) * t.cell}, seed);
			t.rim[(size_t)y] = Ocs(c * 0.55f);
		}

		// The cut face as facets: a jittered lattice split into triangles, each
		// turned a little toward or away from the light and clipped short of the
		// cut. Two halves of a cell that land on the same tone become one quad.
		const Vec2 lo = f.Min(), hi = f.Max();
		const int fx = (int)ceilf((hi.x - lo.x) / kFacetPitch) + 1;
		const int fy = (int)ceilf((hi.y - lo.y) / kFacetPitch) + 1;
		auto lattice = [&](int i, int j)
		{
			Vec2 p(lo.x + i * kFacetPitch, lo.y + j * kFacetPitch);
			if (i > 0 && i < fx - 1) p.x += (hash2(i, j, seed + 101u) - 0.5f) * 2.0f * kFacetJitter;
			if (j > 0 && j < fy - 1) p.y += (hash2(i, j, seed + 103u) - 0.5f) * 2.0f * kFacetJitter;
			p.x = clampf(p.x, lo.x, hi.x);
			p.y = clampf(p.y, lo.y, hi.y);
			return p;
		};
		const Vec3 L3 = normalize(Vec3(kLightDir.x, kLightDir.y, 0.9f));
		auto tone = [&](int i, int j, int half)
		{
			const float rx = (hash2(i * 2 + half, j, seed + 107u) - 0.5f) * 1.3f;
			const float ry = (hash2(i * 2 + half, j, seed + 109u) - 0.5f) * 1.3f;
			const float lambert = dot(normalize(Vec3(rx, ry, 1.0f)), L3);
			return kFacetTone[(int)(saturatef((lambert - 0.55f) / 0.45f) * 3.999f)];
		};
		t.facetPitch = kFacetPitch;
		t.facets.assign((size_t)fy, {});
		for (int j = 0; j + 1 < fy; ++j)
			for (int i = 0; i + 1 < fx; ++i)
			{
				const Vec2 q[4] = {lattice(i, j), lattice(i + 1, j), lattice(i + 1, j + 1), lattice(i, j + 1)};
				float g[4];
				bool any = false, all = true;
				for (int k = 0; k < 4; ++k)
				{
					g[k] = SampleGrid(v, rockGrid, q[k]) - kFacetInset;
					any |= g[k] > 0.0f;
					all &= g[k] > 0.0f;
				}
				if (!any) continue;
				const bool flip = hash2(i, j, seed + 113u) > 0.5f;
				const int tri[2][3] = {{0, 1, flip ? 3 : 2}, {flip ? 1 : 0, 2, 3}};
				Vec4 colour[2];
				for (int h = 0; h < 2; ++h)
				{
					Vec2 c(0, 0);
					for (int k : tri[h]) c += q[k];
					c = c / 3.0f;
					colour[h] = Ocs(Strata(t.strata, t.surfaceY, c, seed) * tone(i, j, h));
				}
				auto add = [&](const Vec2* pts, int n, Vec4 col)
				{
					if (n < 3) return;
					Terrain::Facet facet;
					facet.n = std::min(n, 4);
					facet.colour = col;
					facet.lo = facet.hi = pts[0].x;
					for (int k = 0; k < facet.n; ++k)
					{
						facet.p[k] = pts[k];
						facet.centre += pts[k] / (float)facet.n;
						facet.lo = std::min(facet.lo, pts[k].x);
						facet.hi = std::max(facet.hi, pts[k].x);
					}
					t.facets[(size_t)j].push_back(facet);
				};
				if (all && colour[0].x == colour[1].x && colour[0].y == colour[1].y && colour[0].z == colour[1].z)
				{
					add(q, 4, colour[0]);
					continue;
				}
				for (int h = 0; h < 2; ++h)
				{
					const Vec2 p3[3] = {q[tri[h][0]], q[tri[h][1]], q[tri[h][2]]};
					const float g3[3] = {g[tri[h][0]], g[tri[h][1]], g[tri[h][2]]};
					Vec2 clipped[4];
					const int n = ClipPositive(p3, g3, 3, clipped);
					add(clipped, n, colour[h]);
				}
			}

		// Stars and nebulae far behind the slice, for parallax only.
		Rng sr(seed * 31u + 5u);
		for (int i = 0; i < kStarCount; ++i)
		{
			Terrain::Star s;
			s.p = Vec2(sr.range(lo.x - 60.0f, hi.x + 60.0f), sr.range(lo.y, hi.y + 160.0f));
			s.depth = sr.range(60.0f, 220.0f);
			s.size = sr.range(0.6f, 1.0f);
			s.colour = hsv(0.55f + sr.f01() * 0.18f, 0.30f, sr.range(0.45f, 1.0f));
			t.stars.push_back(s);
		}
		for (int i = 0; i < kNebulaCount; ++i)
		{
			Terrain::Nebula n;
			n.p = Vec2(sr.range(lo.x, hi.x), sr.range(f.escapeY - 60.0f, hi.y + 90.0f));
			n.depth = sr.range(150.0f, 280.0f);
			n.size = sr.range(30.0f, 85.0f);
			// One palette step of the cloud's hue per band, up from the black of
			// space, so the bands stay distinct after the snap to twelve bits.
			const Vec3 hue = hsv(kNebulaHue[family] + sr.range(-0.06f, 0.06f), 0.65f, 1.0f);
			const float step = sr.range(0.045f, 0.065f);
			// The bands do not share a centre, so the cloud is lopsided, not a target.
			for (int b = 0; b < 3; ++b)
			{
				n.bands[b] = kSpace + hue * (step * (b + 1));
				n.drift[b] = b == 0 ? Vec2(0, 0) : Vec2(sr.range(-0.16f, 0.16f), sr.range(-0.12f, 0.12f));
			}
			// A ragged ring of points, and the hull round them: a cloud that is
			// lumpy but still one convex piece.
			std::vector<Vec2> ring;
			const int points = 18;
			for (int k = 0; k < points; ++k)
			{
				const float a = kTau * (k + sr.range(-0.3f, 0.3f)) / points;
				ring.push_back(fromAngle(a) * sr.range(0.84f, 1.0f));
			}
			n.outline = ConvexHull(ring);
			t.nebulae.push_back(std::move(n));
		}
	}

	namespace
	{

		// The bore's colour for one band at one height. Deeper bands are darker and a
		// little more saturated; lower in the planet the whole bore drifts from its
		// own hue toward violet, so every height has its own copper line.
		Vec4 BoreColour(const Terrain& t, int level, float y, const TerrainLook& look)
		{
			const float down = 1.0f - saturatef(y / std::max(1.0f, t.surfaceY));
			const float k = (float)level / (Terrain::kBoreLevels - 1);
			const float hue = t.boreHue.x + down * 0.11f + k * 0.02f;
			const float sat = t.boreHue.y * (0.80f + 0.20f * k);
			const float val = t.boreHue.z * (1.0f - 0.80f * powf(k, 0.8f)) * (0.82f + 0.18f * (1.0f - down));
			Vec3 c = hsv(hue, sat, val);
			c = lerp(c, Vec3(0.75f, 0.12f, 0.05f) * (1.0f - 0.6f * k), look.meltdown * 0.65f);
			return Ocs(c);
		}

	} // namespace

	void DrawSky(Renderer& r, const Terrain& t, float time)
	{
		const float W = r.Width(), H = r.Height();

		// Space, with nebulae and stars far behind for parallax. Every cloud's
		// outer band goes down first, then every middle one, then the cores, so
		// clouds that overlap merge into one bank rather than stacking.
		r.Rect(0.0f, 0.0f, W, H, Ocs(kSpace));
		std::vector<Vec2> cloud;
		for (int b = 0; b < 3; ++b)
			for (const Terrain::Nebula& n : t.nebulae)
			{
				const Vec2 s = r.ToScreen(n.p, n.depth);
				const float rad = n.size * r.ScaleAt(n.depth);
				if (s.x + rad < 0 || s.y + rad < 0 || s.x - rad > W || s.y - rad > H) continue;
				cloud.clear();
				const Vec2 c = s + Vec2(n.drift[b].x, -n.drift[b].y) * rad;
				for (const Vec2& p : n.outline) cloud.push_back(c + p * (rad * kNebulaBand[b]));
				r.ScreenPoly(cloud, Fill::Solid(Ocs(n.bands[b])));
			}
		for (const Terrain::Star& s : t.stars)
		{
			const Vec2 p = r.ToScreen(s.p, s.depth);
			if (p.x < -4 || p.y < -4 || p.x > W + 4 || p.y > H + 4) continue;
			const float tw = 0.70f + 0.30f * sinf(time * 2.0f + s.p.x * 0.1f + s.p.y * 0.07f);
			const float px = s.depth < 120.0f ? 2.0f : 1.0f;
			const Vec4 c = Ocs(s.colour * (tw * s.size));
			r.Rect(p.x, p.y, px, px, c);
			// The nearest few get the four-point glint of a bright star.
			if (s.depth < 80.0f && s.size > 0.9f)
			{
				const Vec4 arm = WithAlpha(c, 0.6f);
				r.Rect(p.x - 2.0f, p.y + 0.5f, 6.0f, 1.0f, arm);
				r.Rect(p.x + 0.5f, p.y - 2.0f, 1.0f, 6.0f, arm);
			}
		}

		// The atmosphere: copper bands along the skyline, one palette colour per
		// band of height, stepping rather than blending, with the next colour
		// dithered in over the last lines of each band like a raster split. It is
		// the planet's own, so it moves with the slice rather than with the stars,
		// and it fades below the skyline as well as above, so a deep valley opens
		// onto space rather than onto a wall of glow. Where it thins out at either
		// end it is drawn on every second line, then every third and fourth, so
		// the stars and nebulae behind come through it a little at a time.
		static const float stops[] = {-16.0f, -6.0f, 0.0f, 4.0f, 10.0f, 20.0f, 34.0f};
		const float thinBelow = stops[1], thinAbove = stops[5];
		const Vec3 space = kSpace;
		const Vec3 tones[] = {space,
		                      lerp(space, t.horizon, 0.35f),
		                      t.horizon * 0.85f,
		                      t.horizon * 0.58f,
		                      lerp(t.horizon, Vec3(0.10f, 0.06f, 0.24f), 0.7f) * 0.5f,
		                      Vec3(0.04f, 0.03f, 0.11f),
		                      space};
		constexpr int kStops = (int)(sizeof(stops) / sizeof(stops[0]));
		auto ramp = [&](float alt)
		{
			for (int i = 0; i + 1 < kStops; ++i)
				if (alt < stops[i + 1])
					return Ocs(lerp(tones[i], tones[i + 1], saturatef((alt - stops[i]) / (stops[i + 1] - stops[i]))));
			return Ocs(space);
		};
		const float k = r.Scale();
		const Vec2 cam = r.CameraCentre();
		auto screenAt = [&](float alt)
		{
			return H * 0.5f - (t.surfaceY + alt - cam.y) * k;
		};
		const float band = 1.5f, ceiling = stops[kStops - 1];
		auto thinness = [&](float a)
		{
			if (a > thinAbove) return saturatef((a - thinAbove) / (ceiling - thinAbove));
			if (a < thinBelow) return saturatef((thinBelow - a) / (thinBelow - stops[0]));
			return 0.0f;
		};
		const Vec4 empty = Ocs(space);
		float alt = std::max(stops[0], floorf((cam.y - H * 0.5f / k - t.surfaceY) / band) * band);
		float bottom = std::min(H, screenAt(alt));
		Vec4 colour = ramp(alt);
		while (bottom > 0.0f && alt < ceiling)
		{
			float next = alt + band;
			Vec4 c = ramp(next);
			while (next < ceiling && c.x == colour.x && c.y == colour.y && c.z == colour.z)
			{
				next += band;
				c = ramp(next);
			}
			const float top = std::max(0.0f, screenAt(next));
			const bool blank = colour.x == empty.x && colour.y == empty.y && colour.z == empty.z;
			if (bottom > top && !blank)
			{
				if (thinness(alt) <= 0.0f && thinness(next) <= 0.0f)
				{
					r.Rect(0.0f, top, W, bottom - top, colour);
					for (int line = 1; line <= 3 && top + line * 2.0f < bottom; ++line)
						r.Rect(0.0f, top + line * 2.0f, W, 1.0f, c);
				}
				else
				{
					for (int y = (int)ceilf(top); y < (int)ceilf(bottom); ++y)
					{
						const float thin = thinness(t.surfaceY - cam.y + (H * 0.5f - (y + 0.5f)) / k);
						const int gap = thin <= 0.0f ? 1 : 2 + (int)(thin * 2.999f);
						if (y % gap == 0) r.Rect(0.0f, (float)y, W, 1.0f, colour);
					}
				}
			}
			bottom = top;
			alt = next;
			colour = c;
		}
	}

	namespace
	{

		// How brightly the lamps fall on a point, and the colour they add: in four hard
		// steps out from each lamp rather than a smooth falloff.
		float LampsAt(std::span<const Lamp> lamps, Vec2 p, Vec3* tint)
		{
			float boost = 0.0f;
			for (const Lamp& l : lamps)
			{
				const float d2 = lengthSq(p - l.pos);
				if (d2 >= l.radius * l.radius) continue;
				const float f = floorf((1.0f - sqrtf(d2) / l.radius) * 4.0f) * 0.25f;
				if (f <= 0.0f) continue;
				boost += f * std::max(l.colour.x, std::max(l.colour.y, l.colour.z));
				*tint += l.colour * f;
			}
			return boost;
		}

	} // namespace

	void DrawTerrain(Renderer& r, const Terrain& t, const TerrainLook& look)
	{
		Vec2 lo, hi;
		r.ViewBox(t.cell * 2.0f, &lo, &hi);
		const int row0 = std::max(0, (int)floorf((lo.y - t.origin.y) / t.cell));
		const int row1 = std::min(t.rows - 1, (int)ceilf((hi.y - t.origin.y) / t.cell));
		auto overlaps = [&](const Piece& p)
		{
			return p.hi.x >= lo.x && p.lo.x <= hi.x;
		};

		// The planet's top seen in perspective behind its front edge: a slice of a
		// planet has a thickness. It is drawn first, so where it falls behind the
		// cut face the rock covers it. Two segments to a quad is close enough: the
		// boundary line is drawn over the front edge.
		const Vec4 top = Ocs(t.stone * 0.55f);
		for (const Terrain::Run& run : t.surface)
		{
			if (!r.Visible(run.lo, run.hi, kSkirt)) continue;
			for (size_t i = 0; i + 1 < run.pts.size(); i += 2)
			{
				const Vec2 a = run.pts[i], b = run.pts[std::min(i + 2, run.pts.size() - 1)];
				const Vec2 quad[4] = {r.ToScreen(a, 0.0f), r.ToScreen(b, 0.0f), r.ToScreen(b, kSkirt),
				                      r.ToScreen(a, kSkirt)};
				r.ScreenPoly(quad, Fill::Solid(top));
			}
		}

		auto draw = [&](const Piece& p, Vec4 c)
		{
			if (p.rect) r.Box(p.lo, p.hi, c);
			else r.Poly(p.pts, Fill::Solid(c));
		};
		for (int row = row0; row <= row1; ++row)
			for (const Piece& p : t.rock[(size_t)row])
				if (overlaps(p)) draw(p, t.rim[(size_t)row]);

		// The cut face is dead stone until something lights it: then each facet
		// takes the light on its own, stepped, so a blast throws the rock into
		// relief one plane at a time.
		if (!t.facets.empty())
		{
			const int f0 = std::max(0, (int)floorf((lo.y - t.origin.y) / t.facetPitch) - 1);
			const int f1 = std::min((int)t.facets.size() - 1, (int)ceilf((hi.y - t.origin.y) / t.facetPitch) + 1);
			for (int j = f0; j <= f1; ++j)
				for (const Terrain::Facet& facet : t.facets[(size_t)j])
				{
					if (facet.hi < lo.x || facet.lo > hi.x) continue;
					Vec4 c = facet.colour;
					if (!look.lamps.empty())
					{
						Vec3 tint(0, 0, 0);
						const float boost = LampsAt(look.lamps, facet.centre, &tint);
						if (boost > 0.0f) c = Ocs(c.xyz() * (1.0f + std::min(boost, 3.0f) * 0.28f) + tint * 0.07f);
					}
					r.Poly(std::span<const Vec2>(facet.p, (size_t)facet.n), Fill::Solid(c));
				}
		}

		for (int k = 0; k < Terrain::kBoreLevels; ++k)
			for (int row = row0; row <= row1; ++row)
			{
				const auto& pieces = t.bore[k][(size_t)row];
				if (pieces.empty()) continue;
				const Vec4 c = BoreColour(t, k, t.origin.y + (row + 0.5f) * t.cell, look);
				for (const Piece& p : pieces)
					if (overlaps(p)) draw(p, c);
			}

		// Lamps pool over the bore: the open cave of each row they reach, drawn
		// again with the lamp's light falling off across it, cut to the lamp's
		// reach so no pixel beyond it pays for the gradient. Only the strongest few
		// are worth the redraw.
		int pools = 0;
		Vec2 window[kPoolSides];
		std::vector<Vec2> lit, scratch;
		for (const Lamp& l : look.lamps)
		{
			const float strength = std::max(l.colour.x, std::max(l.colour.y, l.colour.z));
			if (strength < 0.25f || !r.Visible(l.pos, l.pos, l.radius) || ++pools > kBorePools) continue;
			const Vec3 hue = l.colour / strength;
			const Fill pool = Fill::Radial(l.pos, l.radius, Ocs(hue, std::min(0.45f, strength * 0.16f)),
			                               Ocs(hue, 0.0f));
			const int a = std::max(row0, (int)floorf((l.pos.y - l.radius - t.origin.y) / t.cell));
			const int b = std::min(row1, (int)ceilf((l.pos.y + l.radius - t.origin.y) / t.cell));
			const float outer = l.radius / cosf(kPi / kPoolSides);
			for (int i = 0; i < kPoolSides; ++i) window[i] = l.pos + fromAngle(kTau * i / kPoolSides) * outer;
			for (int row = a; row <= b; ++row)
				for (const Piece& p : t.bore[0][(size_t)row])
				{
					if (!overlaps(p) || p.hi.x < l.pos.x - l.radius || p.lo.x > l.pos.x + l.radius) continue;
					lit.assign(p.pts.begin(), p.pts.end());
					ClipToWindow(lit, window, scratch);
					if (lit.size() >= 3) r.Poly(lit, pool);
				}
		}
	}

	// Each contour is an outline with no fill: a band of constant width centred on
	// the isoline, mitred at its corners, built without cutting anything into
	// triangles. Only the stretches in view are drawn, each closed by a detour
	// round a box well off the screen, where its band is never seen.
	void DrawBoundary(Renderer& r, const Terrain& t, const TerrainLook& look)
	{
		const float width = look.edgeWidth * r.Scale();
		const Fill none = Fill::Solid({0, 0, 0, 0});
		Vec2 lo, hi;
		r.ViewBox(look.edgeWidth + t.cell, &lo, &hi);
		auto inView = [&](Vec2 p)
		{
			return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
		};
		const float W = r.Width(), H = r.Height(), B = width + 16.0f;
		const Vec2 corner[4] = {{W + B, -B}, {W + B, H + B}, {-B, H + B}, {-B, -B}};
		auto side = [&](Vec2 p)
		{
			const float out[4] = {-p.y, p.x - W, p.y - H, -p.x}; // top, right, bottom, left
			int best = 0;
			for (int s = 1; s < 4; ++s)
				if (out[s] > out[best]) best = s;
			return best;
		};
		auto onBox = [&](Vec2 p, int s)
		{
			const float x = clampf(p.x, -B, W + B), y = clampf(p.y, -B, H + B);
			return s == 0 ? Vec2(x, -B) : s == 1 ? Vec2(W + B, y) : s == 2 ? Vec2(x, H + B) : Vec2(-B, y);
		};
		std::vector<Vec2> span;
		for (const Terrain::Contour& c : t.boundary)
		{
			if (!r.Visible(c.lo, c.hi, look.edgeWidth)) continue;
			const size_t n = c.pts.size();
			size_t start = 0;
			while (start < n && inView(c.pts[start])) ++start;
			if (start == n)
			{
				r.Poly(c.pts, none, look.edge, width);
				continue;
			}
			for (size_t k = 0; k < n;)
			{
				if (!inView(c.pts[(start + k) % n]))
				{
					++k;
					continue;
				}
				span.clear();
				span.push_back(r.ToScreen(c.pts[(start + k + n - 1) % n]));
				for (; k < n && inView(c.pts[(start + k) % n]); ++k) span.push_back(r.ToScreen(c.pts[(start + k) % n]));
				span.push_back(r.ToScreen(c.pts[(start + k) % n]));
				const int from = side(span.back()), to = side(span.front());
				span.push_back(onBox(span.back(), from));
				for (int s = from; s != to; s = (s + 1) & 3) span.push_back(corner[s]);
				span.push_back(onBox(span.front(), to));
				r.ScreenPoly(span, none, look.edge, width);
			}
		}
	}

} // namespace zp

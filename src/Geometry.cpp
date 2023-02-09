#include "Geometry.h"

namespace zp {

std::vector<Vec2> RegularPoly(int sides, float radius, float phase) {
    std::vector<Vec2> p;
    p.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        float a = phase + kTau * i / sides;
        p.push_back({cosf(a) * radius, sinf(a) * radius});
    }
    return p;
}

void ExtrudePoly(MeshData& m, const std::vector<Vec2>& poly, float zFront, float zBack,
                 Vec3 faceCol, Vec3 sideCol, float glow) {
    const size_t n = poly.size();
    if (n < 3) return;

    // Front cap faces the camera (-Z), so the fan runs backwards.
    for (size_t i = 1; i + 1 < n; ++i) {
        Vec3 a(poly[0], zFront), b(poly[i + 1], zFront), c(poly[i], zFront);
        m.triN(a, b, c, {0, 0, -1}, faceCol, glow, glow, glow);
    }
    for (size_t i = 1; i + 1 < n; ++i) {
        Vec3 a(poly[0], zBack), b(poly[i], zBack), c(poly[i + 1], zBack);
        m.triN(a, b, c, {0, 0, 1}, faceCol * 0.35f, 0, 0, 0);
    }
    for (size_t i = 0; i < n; ++i) {
        Vec2 p0 = poly[i], p1 = poly[(i + 1) % n];
        Vec2 e = p1 - p0;
        Vec3 nrm = normalize(Vec3(e.y, -e.x, 0));
        Vec3 A(p0, zFront), B(p1, zFront), C(p1, zBack), D(p0, zBack);
        m.triN(A, B, C, nrm, sideCol, glow, glow, 0);
        m.triN(A, C, D, nrm, sideCol, glow, 0, 0);
    }
}

void BiPyramid(MeshData& m, const std::vector<Vec2>& poly, float zFront, float zBack, Vec3 col,
               float glow) {
    const size_t n = poly.size();
    if (n < 3) return;
    Vec3 apexF(0, 0, zFront), apexB(0, 0, zBack);
    for (size_t i = 0; i < n; ++i) {
        Vec3 a(poly[i], 0.0f), b(poly[(i + 1) % n], 0.0f);
        m.tri(apexF, b, a, col, glow * 1.6f, glow, glow);
        m.tri(apexB, a, b, col * 0.7f, glow * 1.2f, glow, glow);
    }
}

// --------------------------------------------------------------- terrain

// The cross section every mesh has to agree on. The rock top is the near
// surface the camera looks down on; the basin sinks away from it wherever the
// field says there is open air. If the lip and the floor disagree here, they
// come apart on screen.
namespace {

// A cave is a bore through rock, so its cross section is a half pipe: the wall
// drops away almost vertically at the cut edge and flattens out along the
// centre line. A bowl reads as a quarry; this reads as a tunnel.
constexpr float kBoreScale = 1.25f;  // how deep the pipe is against its radius
constexpr float kSkirt = 26.0f;      // thickness of the planet against the sky

struct Profile {
    uint32_t seed;

    // The cut through the planet: what you look down on where there is rock.
    // Kept close to the play plane on purpose - every unit of relief here is
    // depth in *front* of the props, which reads as them floating. All the
    // parallax is supposed to come from the bore behind.
    float rockTop(Vec2 p) const {
        float n = fbm(p.x * 0.020f, p.y * 0.020f, 3, seed + 5u) * 0.5f + 0.5f;
        float n2 = fbm(p.x * 0.007f, p.y * 0.007f, 2, seed + 91u) * 0.5f + 0.5f;
        float h = 0.30f + n * 1.0f + n2 * 1.8f;
        // Terraced strata: the planet reads as sliced open rather than painted,
        // and it is why the cap normals come out faceted instead of glassy.
        const float step = 1.0f;
        h = lerpf(h, floorf(h / step) * step + step * 0.5f, 0.8f);
        h += fbm(p.x * 0.16f, p.y * 0.16f, 2, seed + 131u) * 0.35f;
        return h;
    }

    // d = distance to the nearest wall, R = radius of the passage we are in.
    // sqrt(d(2R - d)) is the circle: vertical at the wall, flat at the centre.
    // Both the bore radius and the distance are pushed around by noise so no
    // two stretches of tunnel have the same section.
    float bore(Vec2 p, float d, float R) const {
        if (d <= 0.0f) return 0.0f;
        float wob = fbm(p.x * 0.018f, p.y * 0.018f, 3, seed + 613u);
        float Rv = std::max(3.5f, R * (1.0f + wob * 0.32f));
        float dd = d * (1.0f + fbm(p.x * 0.034f, p.y * 0.034f, 2, seed + 727u) * 0.24f);
        dd = std::min(dd, Rv);
        return sqrtf(std::max(0.0f, dd * (2.0f * Rv - dd)));
    }

    // Broken rock, fading in away from the cut so the edge stays clean and the
    // bore floor is where the torch and the explosions find something to rake.
    float rough(Vec2 p, float d) const {
        float k = saturatef(d / 12.0f);
        if (k <= 0.0f) return 0.0f;
        float z = fbm(p.x * 0.013f, p.y * 0.013f, 2, seed + 401u) * 8.0f;
        z += fbm(p.x * 0.046f, p.y * 0.046f, 4, seed + 211u) * 5.5f;
        z += fabsf(fbm(p.x * 0.108f, p.y * 0.108f, 3, seed + 307u)) * 3.5f;
        return z * k;
    }

    float surfaceZ(Vec2 p, float carved, float radius) const {
        float d = std::max(0.0f, -carved);
        return rockTop(p) + kBoreScale * bore(p, d, radius) + rough(p, d);
    }
};

}  // namespace

// One continuous sheet for the whole planet slice. The marching squares ring is
// taken over the *uncarved* terrain, so the only hard silhouette is the planet
// against the sky; the caverns are bores in the same sheet. Three things
// matter here:
//   * the ambiguous (saddle) cell is split into two lobes, otherwise the fan
//     bridges straight across the empty diagonal;
//   * heights and normals are cached per grid point, so neighbouring cells stay
//     welded and the sheet shades as one surface;
//   * the collision boundary is drawn as its own ribbon at the end. The player
//     flies against a 2D isoline, so that line has to be exact and readable and
//     cannot be left to whatever the shading happens to do.
//
// The cut face and the bore are built as two meshes over the same grid. They
// share the isoline exactly, so they meet without a seam, but they get their
// own material: the cut is dull matte rock, the bore is lit and veined.
static MeshData BuildSheet(const Field& f, uint32_t seed, bool caveSide) {
    MeshData m;
    m.v.reserve(caveSide ? 400000 : 300000);

    const int nx = f.NX(), ny = f.NY();
    const float edgeBand = f.Cell() * 1.4f;
    const Profile prof{seed};

    auto band = [&](float v) { return saturatef(1.0f - fabsf(v) / edgeBand); };

    const size_t total = (size_t)nx * ny;
    std::vector<float> gz(total), gd(total), gb(total), gao(total);
    std::vector<Vec3> gn(total);
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            size_t i = (size_t)y * nx + x;
            gd[i] = f.At(x, y);
            gb[i] = f.AtBase(x, y);
            float radius = f.RadiusAt(x, y);
            gz[i] = prof.surfaceZ(f.CellCentre(x, y), gd[i], radius);
            // The one place a bore shadows itself is the crease where it meets
            // the cut face; the middle of a chamber sees the whole opening.
            // Scaling the reach by the passage radius is what makes a tight
            // neck read as dark all the way across and a hall read as open.
            gao[i] = caveSide
                         ? 0.40f + 0.60f * smoothstepf(0.0f, std::max(4.0f, radius) * 0.55f,
                                                       std::max(0.0f, -gd[i]))
                         : 1.0f;
        }
    // Normals from the neighbouring heights rather than from finite differences
    // of the noise: same result, a fifth of the evaluations.
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            int xm = std::max(0, x - 1), xp = std::min(nx - 1, x + 1);
            int ym = std::max(0, y - 1), yp = std::min(ny - 1, y + 1);
            float dx = (gz[(size_t)y * nx + xp] - gz[(size_t)y * nx + xm]) / ((xp - xm) * f.Cell());
            float dy = (gz[(size_t)yp * nx + x] - gz[(size_t)ym * nx + x]) / ((yp - ym) * f.Cell());
            gn[(size_t)y * nx + x] = normalize(Vec3(dx, dy, -1.0f));
        }

    // Rock is solid and inside the planet; the bore is open and inside the
    // planet. Taking the min of the two tests means the outer silhouette stays
    // the uncarved planet outline for both sheets.
    auto ring = [&](size_t i) { return std::min(caveSide ? -gd[i] : gd[i], gb[i]); };

    Vec2 poly[8];
    float polyZ[8], polyGlow[8], polyBase[8], polyAo[8];
    Vec3 polyN[8];
    bool isCross[8];

    for (int y = 0; y < ny - 1; ++y) {
        for (int x = 0; x < nx - 1; ++x) {
            const size_t ci[4] = {(size_t)y * nx + x, (size_t)y * nx + x + 1,
                                  (size_t)(y + 1) * nx + x + 1, (size_t)(y + 1) * nx + x};
            const float cv[4] = {ring(ci[0]), ring(ci[1]), ring(ci[2]), ring(ci[3])};
            const bool in[4] = {cv[0] > 0, cv[1] > 0, cv[2] > 0, cv[3] > 0};
            if (!in[0] && !in[1] && !in[2] && !in[3]) continue;

            const Vec2 cp[4] = {f.CellCentre(x, y), f.CellCentre(x + 1, y),
                                f.CellCentre(x + 1, y + 1), f.CellCentre(x, y + 1)};

            // Ring of the region, counter clockwise.
            int count = 0;
            for (int k = 0; k < 4; ++k) {
                int k2 = (k + 1) & 3;
                if (in[k]) {
                    poly[count] = cp[k];
                    polyZ[count] = gz[ci[k]];
                    polyN[count] = gn[ci[k]];
                    polyGlow[count] = caveSide ? band(gd[ci[k]]) : 0.0f;
                    polyBase[count] = gb[ci[k]];
                    polyAo[count] = gao[ci[k]];
                    isCross[count] = false;
                    ++count;
                }
                if (in[k] != in[k2]) {
                    float t = cv[k] / (cv[k] - cv[k2]);
                    poly[count] = lerp(cp[k], cp[k2], t);
                    // Interpolating the cached endpoints keeps neighbouring
                    // cells welded exactly along their shared edge.
                    polyZ[count] = lerpf(gz[ci[k]], gz[ci[k2]], t);
                    polyN[count] = normalize(lerp(gn[ci[k]], gn[ci[k2]], t));
                    polyGlow[count] = caveSide ? band(lerpf(gd[ci[k]], gd[ci[k2]], t)) : 0.0f;
                    polyBase[count] = lerpf(gb[ci[k]], gb[ci[k2]], t);
                    polyAo[count] = lerpf(gao[ci[k]], gao[ci[k2]], t);
                    isCross[count] = true;
                    ++count;
                }
            }
            if (count < 3) continue;

            Vec2 mid = cp[0];
            float n1 = fbm(mid.x * 0.026f, mid.y * 0.026f, 3, seed + 17u);
            float n2 = fbm(mid.x * 0.006f, mid.y * 0.006f, 2, seed + 43u);
            float depth = saturatef((gz[ci[0]] - prof.rockTop(mid)) / 55.0f);
            float hue, sat, val;
            if (caveSide) {
                hue = 0.585f + n2 * 0.070f + n1 * 0.020f;
                sat = 0.78f;
                val = (0.078f + (n1 * 0.5f + 0.5f) * 0.100f) * (1.0f - depth * 0.26f);
            } else {
                // The cut face is dead rock. Desaturated, matte, no veins and no
                // plasma - it is the thing the caverns have to stand out from.
                // It still has to read as stone, so it carries more base value
                // than the bore to make up for having no emissive at all.
                hue = 0.075f + n2 * 0.030f;
                sat = 0.19f;
                val = 0.125f + (n1 * 0.5f + 0.5f) * 0.105f;
            }
            // Sedimentary banding: each horizontal layer of rock gets its own
            // brightness, which is what sells the cross section.
            float layer = floorf(mid.y / 4.5f);
            val *= 0.74f + 0.46f * hash2((int)layer, 3, seed + 7u);
            Vec3 col = hsv(hue, sat, val);
            Vec3 wallCol = col * 0.45f;

            auto emit = [&](const int* id, int n) {
                auto push = [&](int k) {
                    m.v.push_back({Vec3(poly[k], polyZ[k]), polyN[k], col * polyAo[k],
                                   polyGlow[k]});
                };
                // Front cap, wound backwards because it faces -Z.
                for (int i = 1; i + 1 < n; ++i) {
                    push(id[0]);
                    push(id[i + 1]);
                    push(id[i]);
                }
                // The one remaining cliff: the planet's own edge against the
                // sky. Cavern edges must never get one, so only segments that
                // actually sit on the uncarved outline are extruded.
                for (int i = 0; i < n; ++i) {
                    int k = id[i], k2 = id[(i + 1) % n];
                    if (!isCross[k] || !isCross[k2]) continue;
                    if (fabsf(polyBase[k]) > 3.0f || fabsf(polyBase[k2]) > 3.0f) continue;
                    Vec2 e = poly[k2] - poly[k];
                    float el = length(e);
                    if (el < 1e-5f) continue;
                    Vec3 nrm(e.y / el, -e.x / el, 0);
                    const int bands = 3;
                    float backA = polyZ[k] + kSkirt, backB = polyZ[k2] + kSkirt;
                    for (int s = 0; s < bands; ++s) {
                        float t0 = (float)s / bands, t1 = (float)(s + 1) / bands;
                        Vec3 A(poly[k], lerpf(polyZ[k], backA, t0));
                        Vec3 B(poly[k2], lerpf(polyZ[k2], backB, t0));
                        Vec3 C(poly[k2], lerpf(polyZ[k2], backB, t1));
                        Vec3 D(poly[k], lerpf(polyZ[k], backA, t1));
                        Vec3 bandCol = wallCol * (1.0f - t0 * 0.60f);
                        m.triN(A, B, C, nrm, bandCol, 0, 0, 0);
                        m.triN(A, C, D, nrm, bandCol, 0, 0, 0);
                    }
                }
            };

            // Opposite corners solid with the diagonal empty: two lobes, not one
            // ring. The cell centre decides whether the rock actually joins.
            const bool saddle = in[0] == in[2] && in[1] == in[3] && in[0] != in[1];
            if (saddle && (cv[0] + cv[1] + cv[2] + cv[3]) <= 0.0f) {
                const int off = in[0] ? 5 : 0;
                const int lobeA[3] = {(off + 0) % 6, (off + 1) % 6, (off + 2) % 6};
                const int lobeB[3] = {(off + 3) % 6, (off + 4) % 6, (off + 5) % 6};
                emit(lobeA, 3);
                emit(lobeB, 3);
            } else {
                int id[8];
                for (int k = 0; k < count; ++k) id[k] = k;
                emit(id, count);
            }
        }
    }

    if (!caveSide) return m;

    // The collision boundary, a ribbon of constant world width. The whole game
    // is flown against this line, so it cannot be left to a shading gradient
    // that thins out wherever the mesh happens to be sparse. Each corner takes
    // its own surface height, so the strip lies over the lip instead of
    // hovering in front of it.
    const float half = 0.27f;
    const Vec3 lineCol(0.30f, 0.55f, 0.80f);
    const Vec3 face(0, 0, -1);

    auto surfAt = [&](Vec2 p) {
        int gx = (int)((p.x - f.Min().x) / f.Cell() + 0.5f);
        int gy = (int)((p.y - f.Min().y) / f.Cell() + 0.5f);
        return prof.surfaceZ(p, f.Sample(p), f.RadiusAt(gx, gy)) - 0.22f;
    };

    // A round joint at each end. Square caps punch out past the line on every
    // corner, which is what made the joins read as blobs.
    auto cap = [&](Vec2 c) {
        const int seg = 6;
        float z = surfAt(c);
        Vec3 centre(c, z);
        for (int i = 0; i < seg; ++i) {
            float a0 = kTau * i / seg, a1 = kTau * (i + 1) / seg;
            Vec2 p0 = c + fromAngle(a0) * half, p1 = c + fromAngle(a1) * half;
            m.triN(centre, Vec3(p1, surfAt(p1)), Vec3(p0, surfAt(p0)), face, lineCol, 1.0f, 1.0f,
                   1.0f);
        }
    };
    auto rib = [&](Vec2 a, Vec2 b) {
        Vec2 dir = b - a;
        float len = length(dir);
        if (len < 1e-4f) return;
        dir = dir / len;
        Vec2 side = perp(dir) * half;
        Vec2 a0 = a + side, b0 = b + side, b1 = b - side, a1 = a - side;
        Vec3 A(a0, surfAt(a0)), B(b0, surfAt(b0)), C(b1, surfAt(b1)), D(a1, surfAt(a1));
        m.triN(A, B, C, face, lineCol, 1.0f, 1.0f, 1.0f);
        m.triN(A, C, D, face, lineCol, 1.0f, 1.0f, 1.0f);
        cap(a);
        cap(b);
    };

    for (int y = 0; y < ny - 1; ++y) {
        for (int x = 0; x < nx - 1; ++x) {
            const float cv[4] = {f.At(x, y), f.At(x + 1, y), f.At(x + 1, y + 1), f.At(x, y + 1)};
            const Vec2 cp[4] = {f.CellCentre(x, y), f.CellCentre(x + 1, y),
                                f.CellCentre(x + 1, y + 1), f.CellCentre(x, y + 1)};
            Vec2 pts[4];
            int np = 0;
            for (int k = 0; k < 4 && np < 4; ++k) {
                int k2 = (k + 1) & 3;
                if ((cv[k] > 0) == (cv[k2] > 0)) continue;
                float t = cv[k] / (cv[k] - cv[k2]);
                pts[np++] = lerp(cp[k], cp[k2], t);
            }
            for (int s = 0; s + 1 < np; s += 2) rib(pts[s], pts[s + 1]);
        }
    }
    return m;
}

MeshData BuildTerrain(const Field& f, uint32_t seed) { return BuildSheet(f, seed, false); }
MeshData BuildCave(const Field& f, uint32_t seed) { return BuildSheet(f, seed, true); }

// ----------------------------------------------------------------- ship
// A lathe. The 2D profile is revolved about the long axis and three fins are
// grown out of the same surface by modulating the radius, so the hull is one
// continuous body with nothing bolted onto it. It rolls in flight, so it has to
// read from every angle - which a flat card never would.
MeshData BuildShip() {
    MeshData m;
    struct Ring {
        float y, r;
    };
    static const Ring prof[] = {{2.05f, 0.03f},  {1.72f, 0.20f},  {1.30f, 0.36f},
                                {0.60f, 0.47f},  {-0.10f, 0.50f}, {-0.72f, 0.48f},
                                {-1.10f, 0.44f}, {-1.42f, 0.38f}};
    const int rings = (int)_countof(prof);
    const int seg = 22;

    auto finAt = [](float y) {
        float t = saturatef((0.35f - y) / 1.75f);
        return powf(t, 1.7f) * 0.92f;
    };
    auto pt = [&](int ri, int si) {
        float a = kTau * si / seg;
        float lobe = powf(std::max(0.0f, cosf(3.0f * a)), 1.3f);
        float r = prof[ri].r + finAt(prof[ri].y) * lobe;
        return Vec3(cosf(a) * r, prof[ri].y, sinf(a) * r);
    };
    auto shade = [&](int ri) {
        float t = (float)ri / (rings - 1);
        return lerp(Vec3(0.50f, 0.78f, 1.10f), Vec3(0.13f, 0.20f, 0.38f), t);
    };

    for (int ri = 0; ri + 1 < rings; ++ri) {
        Vec3 col = (shade(ri) + shade(ri + 1)) * 0.5f;
        float glow = ri >= rings - 2 ? 0.45f : 0.10f;
        for (int si = 0; si < seg; ++si) {
            int s2 = (si + 1) % seg;
            Vec3 a = pt(ri, si), b = pt(ri, s2), c = pt(ri + 1, s2), d = pt(ri + 1, si);
            m.tri(a, b, c, col, glow, glow, glow);
            m.tri(a, c, d, col, glow, glow, glow);
        }
    }

    Vec3 nose(0, prof[0].y + 0.10f, 0);
    Vec3 tail(0, prof[rings - 1].y - 0.08f, 0);
    for (int si = 0; si < seg; ++si) {
        int s2 = (si + 1) % seg;
        m.tri(nose, pt(0, s2), pt(0, si), shade(0), 0.25f, 0.1f, 0.1f);
        // The tail cap is the engine mouth, so it is the one hot part.
        m.tri(tail, pt(rings - 1, si), pt(rings - 1, s2), Vec3(1.1f, 1.9f, 2.9f), 1.7f, 0.7f,
              0.7f);
    }
    return m;
}

// The orb: a sphere with an equator band, so its spin is legible on the rope.
MeshData BuildPod() {
    MeshData m;
    BiPyramid(m, RegularPoly(12, 1.30f), -1.30f, 1.30f, Vec3(0.70f, 0.35f, 1.10f), 0.85f);
    BiPyramid(m, RegularPoly(12, 0.92f), -1.55f, 1.55f, Vec3(1.60f, 0.80f, 2.40f), 1.6f);
    std::vector<Vec2> band = {{-1.34f, -0.13f}, {1.34f, -0.13f}, {1.34f, 0.13f}, {-1.34f, 0.13f}};
    ExtrudePoly(m, band, -0.40f, 0.40f, Vec3(2.2f, 1.2f, 2.8f), Vec3(1.2f, 0.6f, 1.8f), 1.4f);
    return m;
}

// The cradle the orb is sitting in when the player arrives, and which stays
// behind once it is lifted.
MeshData BuildPedestal() {
    MeshData m;
    Vec3 c(0.22f, 0.24f, 0.34f);
    std::vector<Vec2> plinth = {{-1.70f, -1.60f}, {1.70f, -1.60f}, {1.20f, -0.25f},
                                {-1.20f, -0.25f}};
    ExtrudePoly(m, plinth, -1.20f, 1.20f, c, c * 0.62f, 0.25f);
    std::vector<Vec2> cradle = {{-1.10f, -0.30f}, {1.10f, -0.30f}, {0.85f, 0.45f},
                                {-0.85f, 0.45f}};
    ExtrudePoly(m, cradle, -0.85f, 0.85f, c * 1.35f, c * 0.80f, 0.55f);
    return m;
}

// Fuel: a tank box on legs, stencilled FUEL by the game in world space.
MeshData BuildFuelCell() {
    MeshData m;
    Vec3 shell(0.15f, 0.40f, 0.29f), trim(0.09f, 0.24f, 0.19f);
    std::vector<Vec2> box = {{-1.15f, -0.35f}, {1.15f, -0.35f}, {1.15f, 1.05f}, {-1.15f, 1.05f}};
    ExtrudePoly(m, box, -0.85f, 0.85f, shell, trim, 0.22f);
    std::vector<Vec2> rim = {{-1.30f, 0.98f}, {1.30f, 0.98f}, {1.30f, 1.20f}, {-1.30f, 1.20f}};
    ExtrudePoly(m, rim, -0.95f, 0.95f, Vec3(0.25f, 0.90f, 0.55f), Vec3(0.15f, 0.50f, 0.35f), 0.9f);
    std::vector<Vec2> legL = {{-1.02f, -1.35f}, {-0.72f, -1.35f}, {-0.62f, -0.35f},
                              {-0.92f, -0.35f}};
    std::vector<Vec2> legR = {{0.72f, -1.35f}, {1.02f, -1.35f}, {0.92f, -0.35f}, {0.62f, -0.35f}};
    ExtrudePoly(m, legL, -0.35f, 0.35f, trim, trim * 0.70f, 0.20f);
    ExtrudePoly(m, legR, -0.35f, 0.35f, trim, trim * 0.70f, 0.20f);
    return m;
}

MeshData BuildTurretBase() {
    MeshData m;
    Vec3 c(0.26f, 0.22f, 0.32f);
    // Mounting plate that sinks into the rock, plus the socket the barrel
    // swings in. Local +y points out of the wall.
    std::vector<Vec2> plate = {{-2.40f, -1.60f}, {2.40f, -1.60f}, {1.85f, 0.35f},
                               {-1.85f, 0.35f}};
    ExtrudePoly(m, plate, -1.60f, 1.90f, c, c * 0.60f, 0.35f);
    std::vector<Vec2> socket = RegularPoly(8, 1.10f);
    for (auto& p : socket) p.y += 0.50f;
    ExtrudePoly(m, socket, -1.05f, 1.45f, c * 1.30f, c * 0.75f, 0.55f);
    std::vector<Vec2> lamp = {{-0.30f, 0.20f}, {0.30f, 0.20f}, {0.30f, 0.70f}, {-0.30f, 0.70f}};
    ExtrudePoly(m, lamp, -1.30f, -1.05f, Vec3(0.9f, 0.25f, 0.30f), Vec3(0.5f, 0.15f, 0.2f), 1.4f);
    return m;
}

MeshData BuildTurretBarrel() {
    MeshData m;
    std::vector<Vec2> barrel = {{-0.34f, -0.30f}, {0.34f, -0.30f}, {0.26f, 2.70f},
                                {-0.26f, 2.70f}};
    ExtrudePoly(m, barrel, -0.36f, 0.36f, Vec3(0.55f, 0.28f, 0.34f), Vec3(0.33f, 0.15f, 0.21f),
                0.80f);
    std::vector<Vec2> muzzle = {{-0.44f, 2.45f}, {0.44f, 2.45f}, {0.44f, 2.85f}, {-0.44f, 2.85f}};
    ExtrudePoly(m, muzzle, -0.48f, 0.48f, Vec3(0.70f, 0.30f, 0.35f), Vec3(0.40f, 0.16f, 0.22f),
                1.10f);
    return m;
}

// The factory: a sphere on a base with a chimney, the way the original read it.
MeshData BuildReactorRing() {
    MeshData m;
    Vec3 c(0.30f, 0.26f, 0.38f);
    std::vector<Vec2> base = {{-4.20f, -3.60f}, {4.20f, -3.60f}, {3.10f, -1.70f},
                              {-3.10f, -1.70f}};
    ExtrudePoly(m, base, -2.40f, 2.40f, c, c * 0.60f, 0.30f);
    std::vector<Vec2> stack = {{0.95f, 1.20f}, {2.05f, 1.20f}, {2.05f, 4.60f}, {0.95f, 4.60f}};
    ExtrudePoly(m, stack, -1.00f, 1.00f, c * 1.10f, c * 0.70f, 0.35f);
    std::vector<Vec2> cap = {{0.68f, 4.45f}, {2.32f, 4.45f}, {2.32f, 5.10f}, {0.68f, 5.10f}};
    ExtrudePoly(m, cap, -1.20f, 1.20f, Vec3(0.60f, 0.40f, 0.30f), Vec3(0.35f, 0.22f, 0.18f), 0.6f);
    std::vector<Vec2> collar = RegularPoly(10, 2.05f);
    ExtrudePoly(m, collar, -1.70f, 1.70f, c * 0.85f, c * 0.55f, 0.25f);
    return m;
}

MeshData BuildReactorCore() {
    MeshData m;
    BiPyramid(m, RegularPoly(14, 2.45f), -2.55f, 2.55f, Vec3(2.4f, 1.5f, 0.5f), 2.0f);
    return m;
}

MeshData BuildBullet() {
    MeshData m;
    BiPyramid(m, RegularPoly(3, 0.22f), -0.55f, 0.55f, Vec3(2.4f, 2.0f, 1.2f), 2.0f);
    return m;
}

// Spiked drum. The silhouette has to read as "do not touch" at a glance, so the
// spikes are long relative to the body and the core is a separate hot element
// that the game pulses on the fuse.
MeshData BuildMine() {
    MeshData m;
    std::vector<Vec2> star;
    const int pts = 8;
    for (int i = 0; i < pts * 2; ++i) {
        float a = kTau * i / (pts * 2) + 0.11f;
        float r = (i & 1) ? 0.62f : 1.45f;
        star.push_back({cosf(a) * r, sinf(a) * r});
    }
    BiPyramid(m, star, -0.75f, 0.75f, Vec3(0.42f, 0.34f, 0.20f), 0.35f);
    BiPyramid(m, RegularPoly(6, 0.55f), -1.05f, 1.05f, Vec3(2.4f, 1.7f, 0.30f), 2.0f);
    return m;
}

// Forward swept dart: the nose tells you which way it is committed to going.
MeshData BuildDrone() {
    MeshData m;
    std::vector<Vec2> hull = {{0.0f, 1.70f},  {-0.62f, 0.35f}, {-1.45f, -0.75f},
                              {-0.40f, -0.30f}, {0.0f, -0.85f}, {0.40f, -0.30f},
                              {1.45f, -0.75f}, {0.62f, 0.35f}};
    BiPyramid(m, hull, -0.55f, 0.50f, Vec3(0.72f, 0.20f, 0.26f), 0.45f);
    std::vector<Vec2> spine = {{-0.26f, -0.55f}, {0.26f, -0.55f}, {0.16f, 0.95f}, {-0.16f, 0.95f}};
    ExtrudePoly(m, spine, -0.30f, 0.30f, Vec3(0.30f, 0.10f, 0.14f), Vec3(0.20f, 0.07f, 0.10f),
                0.5f);
    return m;
}

MeshData BuildDroneEye() {
    MeshData m;
    BiPyramid(m, RegularPoly(3, 0.36f, kPi * 0.5f), -0.85f, 0.55f, Vec3(2.8f, 0.9f, 0.7f), 2.2f);
    return m;
}

// ----------------------------------------------------------------- props
// Unit sized: the game scales each instance to whatever the generator asked
// for. The silhouettes have to separate at a glance - volatile things are round
// and banded, inert things are flat sided.

MeshData BuildTank() {
    MeshData m;
    ExtrudePoly(m, RegularPoly(9, 1.0f, 0.15f), -0.85f, 0.85f, Vec3(0.62f, 0.38f, 0.14f),
                Vec3(0.38f, 0.22f, 0.09f), 0.45f);
    // End caps, so a drum never reads as a boulder.
    ExtrudePoly(m, RegularPoly(9, 0.62f, 0.15f), -1.05f, -0.80f, Vec3(0.30f, 0.22f, 0.16f),
                Vec3(0.22f, 0.16f, 0.12f), 0.3f);
    ExtrudePoly(m, RegularPoly(9, 0.62f, 0.15f), 0.80f, 1.05f, Vec3(0.30f, 0.22f, 0.16f),
                Vec3(0.22f, 0.16f, 0.12f), 0.3f);
    return m;
}

MeshData BuildTankCore() {
    MeshData m;
    std::vector<Vec2> band = {{-1.02f, -0.30f}, {1.02f, -0.30f}, {1.02f, 0.30f}, {-1.02f, 0.30f}};
    ExtrudePoly(m, band, -0.90f, 0.90f, Vec3(2.6f, 1.1f, 0.25f), Vec3(2.0f, 0.7f, 0.2f), 2.0f);
    return m;
}

MeshData BuildCrate() {
    MeshData m;
    std::vector<Vec2> box = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    ExtrudePoly(m, box, -0.85f, 0.85f, Vec3(0.30f, 0.34f, 0.44f), Vec3(0.18f, 0.21f, 0.30f), 0.30f);
    std::vector<Vec2> brace = {{-1.02f, -0.16f}, {1.02f, -0.16f}, {1.02f, 0.16f}, {-1.02f, 0.16f}};
    ExtrudePoly(m, brace, -0.90f, 0.90f, Vec3(0.55f, 0.75f, 1.00f), Vec3(0.30f, 0.45f, 0.70f),
                0.7f);
    return m;
}

// Natural rock, so it gets the cave's own vocabulary: an irregular solid with a
// different noise field on every ring, faceted, in the same palette as the
// walls. Man made props are allowed to be smooth and geometric; this is not.
MeshData BuildBoulder() {
    MeshData m;
    const int seg = 15;
    const int rings = 5;
    const float zs[rings] = {-0.92f, -0.58f, 0.0f, 0.58f, 0.92f};
    const float rs[rings] = {0.32f, 0.74f, 1.0f, 0.74f, 0.32f};

    auto pt = [&](int ri, int si) {
        float a = kTau * si / seg;
        float n = fbm(cosf(a) * 1.9f + 4.0f, sinf(a) * 1.9f + zs[ri] * 2.2f, 3, 8123u);
        float r = rs[ri] * (0.78f + n * 0.58f);
        return Vec3(cosf(a) * r, sinf(a) * r, zs[ri] * 0.85f);
    };
    auto shade = [&](int ri, int si) {
        float n = fbm(cosf(kTau * si / seg) * 2.3f, sinf(kTau * si / seg) * 2.3f + ri * 1.7f, 2,
                      991u);
        return hsv(0.572f + n * 0.030f, 0.50f, 0.055f + (n * 0.5f + 0.5f) * 0.060f);
    };

    for (int ri = 0; ri + 1 < rings; ++ri)
        for (int si = 0; si < seg; ++si) {
            int s2 = (si + 1) % seg;
            Vec3 a = pt(ri, si), b = pt(ri, s2), c = pt(ri + 1, s2), d = pt(ri + 1, si);
            Vec3 col = shade(ri, si);
            m.tri(a, b, c, col, 0, 0, 0);
            m.tri(a, c, d, col, 0, 0, 0);
        }

    Vec3 front(0, 0, zs[0] * 0.85f - 0.16f);
    Vec3 back(0, 0, zs[rings - 1] * 0.85f + 0.16f);
    for (int si = 0; si < seg; ++si) {
        int s2 = (si + 1) % seg;
        m.tri(front, pt(0, s2), pt(0, si), shade(0, si), 0, 0, 0);
        m.tri(back, pt(rings - 1, si), pt(rings - 1, s2), shade(rings - 1, si), 0, 0, 0);
    }
    return m;
}

MeshData BuildSlab() {
    MeshData m;
    std::vector<Vec2> box = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    ExtrudePoly(m, box, -0.9f, 0.9f, Vec3(0.26f, 0.30f, 0.38f), Vec3(0.15f, 0.18f, 0.26f), 0.25f);
    return m;
}

MeshData BuildRelic() {
    MeshData m;
    BiPyramid(m, RegularPoly(5, 0.72f, 0.3f), -1.30f, 1.30f, Vec3(2.2f, 1.9f, 0.6f), 1.8f);
    return m;
}

MeshData BuildWellRing() {
    MeshData m;
    std::vector<Vec2> ring;
    const int n = 9;
    for (int i = 0; i < n * 2; ++i) {
        float a = kTau * i / (n * 2);
        float r = (i & 1) ? 2.15f : 3.40f;
        ring.push_back({cosf(a) * r, sinf(a) * r});
    }
    ExtrudePoly(m, ring, -1.05f, 1.55f, Vec3(0.20f, 0.19f, 0.34f), Vec3(0.13f, 0.12f, 0.24f), 0.8f);
    return m;
}

MeshData BuildWellCore() {
    MeshData m;
    BiPyramid(m, RegularPoly(5, 1.30f, 0.31f), -1.8f, 1.8f, Vec3(1.6f, 1.4f, 2.6f), 2.2f);
    return m;
}

}  // namespace zp

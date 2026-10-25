#include "Field.h"

#include <cstdio>
#include <utility>

namespace zp {

// Kept local so Field does not have to know about the profile.
static const int kPlanetColsLocal = 7;
static const int kPlanetRowsLocal = 5;

// One row of the planet grid per star system, seven planets across. The mix of
// shapes inside a row matters more than the shape of any single planet.
static const int kRowArch[kPlanetRowsLocal][kPlanetColsLocal] = {
    {ARCH_SHAFT, ARCH_CHAIN, ARCH_SHAFT, ARCH_SILO, ARCH_CHAIN, ARCH_GALLERY, ARCH_TWIN},
    {ARCH_CHAIN, ARCH_GALLERY, ARCH_POWDERKEG, ARCH_SHAFT, ARCH_SILO, ARCH_PENDULUM, ARCH_FORTRESS},
    {ARCH_TWIN, ARCH_POWDERKEG, ARCH_PENDULUM, ARCH_GALLERY, ARCH_CHAIN, ARCH_SILO, ARCH_FORTRESS},
    {ARCH_PENDULUM, ARCH_SHAFT, ARCH_FORTRESS, ARCH_POWDERKEG, ARCH_TWIN, ARCH_GALLERY, ARCH_SILO},
    {ARCH_FORTRESS, ARCH_PENDULUM, ARCH_TWIN, ARCH_SILO, ARCH_POWDERKEG, ARCH_CHAIN, ARCH_FORTRESS},
};

const char* ArchetypeName(int a) {
    switch (a) {
        case ARCH_SHAFT: return "DESCENT SHAFT";
        case ARCH_CHAIN: return "CAVERN CHAIN";
        case ARCH_SILO: return "OPEN SILO";
        case ARCH_TWIN: return "TWIN CHIMNEY";
        case ARCH_GALLERY: return "LONG GALLERY";
        case ARCH_FORTRESS: return "FORTRESS";
        case ARCH_PENDULUM: return "PENDULUM RUN";
        default: return "POWDERKEG";
    }
}

const char* MedalName(int m) {
    switch (m) {
        case 3: return "GOLD";
        case 2: return "SILVER";
        case 1: return "BRONZE";
        default: return "NO MEDAL";  // only ever shown for a planet already flown
    }
}

// A named place is remembered; "level 23" is not.
static void MakeName(LevelSpec& s) {
    static const char* kHead[] = {"KA", "VE",  "TOR", "MYR", "ZAN", "OL",  "HES", "DRA",
                                  "URN", "XI",  "BEL", "CY",  "NOK", "PHA", "RIG", "SUL"};
    static const char* kTail[] = {"RA",  "DOS", "LIN", "THA", "VOR", "MIS", "KAR",
                                  "NEB", "TIS", "LUM", "GAR", "REN", "ZUL", "PHOS"};
    static const char* kSuffix[] = {"", "", " I", " II", " III", " IV", " IX", " XII",
                                    " PRIME", " MINOR", " DEEP", " REACH"};
    Rng r(s.seed ^ 0xA53F27C1u);
    sprintf_s(s.name, "%s%s%s", kHead[r.next() % 16], kTail[r.next() % 14],
              kSuffix[r.next() % 12]);
}

LevelSpec LevelSpec::For(int index) {
    const int total = kPlanetRowsLocal * kPlanetColsLocal;
    index = std::max(0, std::min(total - 1, index));

    LevelSpec s;
    s.index = index;
    s.row = index / kPlanetColsLocal;
    s.col = index % kPlanetColsLocal;

    uint32_t h = (uint32_t)index * 2654435761u + 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    s.seed = h | 1u;

    s.archetype = kRowArch[s.row][s.col];
    s.archetypeName = ArchetypeName(s.archetype);

    const float t = (float)index / (float)(total - 1);  // 0..1 across the game
    const bool boss = s.col == kPlanetColsLocal - 1;

    // Gravity barely moves. It is a feel constant - varying it wildly just makes
    // the player relearn the ship every planet. The level does the varying.
    s.gravity = 4.6f + s.row * 0.34f + (boss ? 0.35f : 0.0f);
    s.darkness = (s.row >= 3 && (s.col % 3) == 2) ? 1.0f : 0.0f;

    // Early caverns are roomy. Space is the cheapest difficulty dial there is:
    // a wide bore forgives a clumsy approach without changing any other rule.
    s.tunnelRadius = 20.0f - t * 7.0f;
    s.pinch = 0.35f + 0.65f * t;
    s.bulletSpeed = 23.0f + t * 13.0f;
    s.fireInterval = std::max(1.1f, 3.2f - t * 1.3f);
    s.fuelCount = 5 + (int)(t * 3.0f);
    s.relicCount = 3;

    // A hazard is introduced weak and grows over the next several planets, so
    // the planet that brings it in is a lesson rather than an ambush.
    auto ramp = [index](int first, int growOver) {
        if (index < first) return 0.0f;
        return 0.45f + 0.55f * saturatef((float)(index - first) / (float)growOver);
    };

    s.turretCount = std::min(9, 2 + index / 3 + (boss ? 1 : 0));
    s.tankCount = index >= 1 ? std::min(12, 1 + index / 4) : 0;
    s.crateCount = index >= 2 ? std::min(10, 2 + index / 5) : 0;
    s.boulderCount = index >= 2 ? std::min(6, 1 + index / 7) : 0;
    s.mineCount = index >= 5 ? std::min(10, 2 + (index - 5) / 3) : 0;
    s.platformCount = index >= 7 ? std::min(4, 1 + index / 9) : 0;
    s.droneCount = index >= 9 ? std::min(6, 1 + (index - 9) / 5) : 0;
    s.wellCount = index >= 13 ? std::min(3, 1 + (index - 13) / 9) : 0;
    s.repulsorCount = index >= 18 ? std::min(2, 1 + (index - 18) / 10) : 0;
    s.reactorHits = index >= 3 ? std::min(6, 2 + index / 8) : 0;

    s.mineStrength = ramp(5, 14);
    s.droneStrength = ramp(9, 14);
    s.wellStrength = ramp(13, 12);

    if (s.archetype == ARCH_POWDERKEG) {
        s.tankCount = std::min(16, s.tankCount + 6);
        s.crateCount += 3;
    }
    if (s.archetype == ARCH_FORTRESS) s.turretCount = std::min(11, s.turretCount + 3);
    if (s.archetype == ARCH_SHAFT) s.tunnelRadius *= 0.82f;
    if (s.archetype == ARCH_SILO) s.tunnelRadius *= 1.1f;

    // The first three planets are close to unfailable on purpose. What they
    // teach is that the field saves you - which needs something shooting at it,
    // so they get turrets and nothing else.
    if (index < 3) {
        s.turretCount = 1 + index;
        s.mineCount = s.droneCount = 0;
        s.mineStrength = s.droneStrength = 0;
        s.boulderCount = index >= 2 ? 1 : 0;
        s.platformCount = 0;
        s.fuelCount = 6;
    }

    MakeName(s);
    return s;
}

float Field::At(int x, int y) const {
    x = std::max(0, std::min(nx_ - 1, x));
    y = std::max(0, std::min(ny_ - 1, y));
    return v_[(size_t)y * nx_ + x];
}

float Field::AtBase(int x, int y) const {
    x = std::max(0, std::min(nx_ - 1, x));
    y = std::max(0, std::min(ny_ - 1, y));
    return base_[(size_t)y * nx_ + x];
}

float Field::Sample(Vec2 p) const {
    float gx = (p.x - min_.x) / cell_;
    float gy = (p.y - min_.y) / cell_;
    if (gy > (float)(ny_ - 1)) return -80.0f;  // open sky above the map
    gx = clampf(gx, 0.0f, (float)(nx_ - 1) - 0.001f);
    gy = clampf(gy, 0.0f, (float)(ny_ - 1) - 0.001f);
    int x0 = (int)gx, y0 = (int)gy;
    float fx = gx - x0, fy = gy - y0;
    float a = At(x0, y0), b = At(x0 + 1, y0), c = At(x0, y0 + 1), d = At(x0 + 1, y0 + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

Vec2 Field::Gradient(Vec2 p) const {
    float h = cell_ * 0.75f;
    float dx = Sample({p.x + h, p.y}) - Sample({p.x - h, p.y});
    float dy = Sample({p.x, p.y + h}) - Sample({p.x, p.y - h});
    return {dx / (2 * h), dy / (2 * h)};
}

bool Field::SphereHit(Vec2 p, float radius, Vec2* normalOut, float* depthOut) const {
    float f = Sample(p);
    Vec2 g = Gradient(p);
    float gl = length(g);
    if (gl < 1e-4f) {
        if (f <= 0) return false;
        if (normalOut) *normalOut = {0, 1};
        if (depthOut) *depthOut = radius;
        return true;
    }
    float dist = f / gl;  // first order distance to the rock surface
    if (dist < -radius) return false;
    if (normalOut) *normalOut = g * (-1.0f / gl);
    if (depthOut) *depthOut = dist + radius;
    return true;
}

void Field::CarveCapsule(Vec2 a, Vec2 b, float r0, float r1) {
    Vec2 ab = b - a;
    float len2 = std::max(lengthSq(ab), 1e-6f);
    float rmax = std::max(r0, r1) + cell_ * 2.0f;
    float lo_x = std::min(a.x, b.x) - rmax, hi_x = std::max(a.x, b.x) + rmax;
    float lo_y = std::min(a.y, b.y) - rmax, hi_y = std::max(a.y, b.y) + rmax;
    int x0 = std::max(0, (int)((lo_x - min_.x) / cell_));
    int x1 = std::min(nx_ - 1, (int)((hi_x - min_.x) / cell_) + 1);
    int y0 = std::max(0, (int)((lo_y - min_.y) / cell_));
    int y1 = std::min(ny_ - 1, (int)((hi_y - min_.y) / cell_) + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            Vec2 p = CellCentre(x, y);
            float t = clampf(dot(p - a, ab) / len2, 0.0f, 1.0f);
            Vec2 q = a + ab * t;
            float d = length(p - q) - lerpf(r0, r1, t);
            float& cellv = v_[(size_t)y * nx_ + x];
            cellv = std::min(cellv, d);
        }
}

void Field::CarveDisc(Vec2 c, float r) { CarveCapsule(c, c, r, r); }

void Field::AddDisc(Vec2 c, float r) {
    int x0 = std::max(0, (int)((c.x - r - min_.x) / cell_));
    int x1 = std::min(nx_ - 1, (int)((c.x + r - min_.x) / cell_) + 1);
    int y0 = std::max(0, (int)((c.y - r - min_.y) / cell_));
    int y1 = std::min(ny_ - 1, (int)((c.y + r - min_.y) / cell_) + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            Vec2 p = CellCentre(x, y);
            float d = r - length(p - c);
            float& cellv = v_[(size_t)y * nx_ + x];
            cellv = std::max(cellv, d);
        }
}

float Field::RadiusAt(int x, int y) const {
    if (passage_.empty()) return 0.0f;
    x = std::max(0, std::min(nx_ - 1, x));
    y = std::max(0, std::min(ny_ - 1, y));
    return passage_[(size_t)y * nx_ + x];
}

// The radius of the largest inscribed circle that still contains each point.
// A plain distance transform gives the distance to the nearest wall; this gives
// the size of the passage you are standing in, which is what the half pipe
// cross section needs. Propagated as (radius, remaining reach) so a wide
// chamber's radius carries out to its own walls but no further.
void Field::ComputeRadius() {
    const size_t n = (size_t)nx_ * ny_;
    passage_.assign(n, 0.0f);
    std::vector<float> slack(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float d = std::max(0.0f, -v_[i]);
        passage_[i] = d;
        slack[i] = d;
    }

    const float diag = cell_ * 1.4142136f;
    auto relax = [&](int x, int y, int ox, int oy, float cost) {
        size_t a = (size_t)y * nx_ + x;
        size_t b = (size_t)(y + oy) * nx_ + (x + ox);
        float s = slack[b] - cost;
        if (s >= 0.0f && passage_[b] > passage_[a]) {
            passage_[a] = passage_[b];
            slack[a] = s;
        }
    };

    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 1; y < ny_; ++y)
            for (int x = 1; x < nx_ - 1; ++x) {
                relax(x, y, -1, 0, cell_);
                relax(x, y, 0, -1, cell_);
                relax(x, y, -1, -1, diag);
                relax(x, y, 1, -1, diag);
            }
        for (int y = ny_ - 2; y >= 0; --y)
            for (int x = nx_ - 2; x >= 1; --x) {
                relax(x, y, 1, 0, cell_);
                relax(x, y, 0, 1, cell_);
                relax(x, y, 1, 1, diag);
                relax(x, y, -1, 1, diag);
            }
    }
}

bool Field::SnapToWall(Vec2 from, float standoff, Vec2* pos, Vec2* normal) const {
    Vec2 p = from;
    Vec2 n(0, 1);
    for (int i = 0; i < 4; ++i) {
        float val = Sample(p);
        Vec2 g = Gradient(p);
        float gl = length(g);
        if (gl < 1e-4f) return false;
        n = g * (-1.0f / gl);
        p = p + n * (val / gl);  // val is negative in open air, so this walks in
    }
    if (pos) *pos = p + n * standoff;
    if (normal) *normal = n;
    return true;
}

Vec2 Field::FindFloor(Vec2 from) const {
    Vec2 p = from;
    for (int i = 0; i < 300; ++i) {
        if (Sample({p.x, p.y - 1.0f}) > 0.0f) return {p.x, p.y + 1.5f};
        p.y -= 1.0f;
        if (p.y < min_.y + 4.0f) break;
    }
    return from;
}

bool Field::FindWall(Vec2 from, Vec2 dir, float maxDist, Vec2* hit, Vec2* normal) const {
    Vec2 d = normalize(dir);
    float t = 0;
    while (t < maxDist) {
        Vec2 p = from + d * t;
        if (Sample(p) > 0.0f) {
            float lo = std::max(0.0f, t - 1.5f), hi = t;
            for (int i = 0; i < 12; ++i) {
                float mid = (lo + hi) * 0.5f;
                if (Sample(from + d * mid) > 0.0f)
                    hi = mid;
                else
                    lo = mid;
            }
            Vec2 s = from + d * hi;
            Vec2 g = Gradient(s);
            float gl = length(g);
            if (gl < 1e-4f) return false;
            if (hit) *hit = s;
            if (normal) *normal = g * (-1.0f / gl);
            return true;
        }
        t += 1.5f;
    }
    return false;
}

bool Field::FindOpen(Rng& rng, Vec2 around, float spread, float clearance, Vec2* out) const {
    for (int i = 0; i < 48; ++i) {
        Vec2 p = around + rng.unitVec() * (sqrtf(rng.f01()) * spread);
        if (p.x < min_.x + 8.0f || p.x > max_.x - 8.0f) continue;
        if (p.y < min_.y + 8.0f || p.y > max_.y - 8.0f) continue;
        if (Sample(p) < -clearance) {
            if (out) *out = p;
            return true;
        }
    }
    return false;
}

Vec2 Field::FindCeiling(Vec2 from) const {
    Vec2 p = from;
    for (int i = 0; i < 300; ++i) {
        if (Sample({p.x, p.y + 1.0f}) > 0.0f) return {p.x, p.y - 1.5f};
        p.y += 1.0f;
        if (p.y > max_.y - 4.0f) break;
    }
    return from;
}

// --------------------------------------------------------------- terrain

void Field::Terrain(const LevelSpec& spec) {
    const float H = max_.y;
    surfaceBase_ = H * 0.74f;
    surfAmp_ = H * 0.055f;
    for (int y = 0; y < ny_; ++y) {
        for (int x = 0; x < nx_; ++x) {
            Vec2 p = CellCentre(x, y);
            float surf = surfaceBase_ + fbm(p.x * 0.0095f, 3.7f, 3, spec.seed) * surfAmp_;
            float f = surf - p.y;
            f += fbm(p.x * 0.055f, p.y * 0.055f, 4, spec.seed + 11u) * 3.2f;
            f += fbm(p.x * 0.017f, p.y * 0.017f, 3, spec.seed + 31u) * 7.0f;
            f = std::max(f, 12.0f - (p.x - min_.x));
            f = std::max(f, 12.0f - (max_.x - p.x));
            f = std::max(f, 12.0f - (p.y - min_.y));
            v_[(size_t)y * nx_ + x] = f;
        }
    }
    base_ = v_;
}

// ----------------------------------------------------------------- spine

// Each archetype is a different shape of problem, not a different noise seed.
// This is where authored intent enters a procedural level.
void Field::Spine(const LevelSpec& spec, Rng& rng) {
    const float W = max_.x, H = max_.y;
    const float margin = 52.0f;
    path.clear();
    radii_.clear();

    entranceX_ = rng.range(W * 0.30f, W * 0.70f);
    entranceY_ = surfaceBase_ + fbm(entranceX_ * 0.0095f, 3.7f, 3, spec.seed) * surfAmp_;

    int steps = 13;
    float dyMin = 12, dyMax = 21, dxMin = 6, dxMax = 30, radScale = 1.0f, turn = 0.30f;
    switch (spec.archetype) {
        case ARCH_SHAFT: steps = 15; dyMin = 13; dyMax = 19; dxMin = 2; dxMax = 12;
                         radScale = 0.74f; turn = 0.45f; break;
        case ARCH_CHAIN: steps = 11; dyMin = 14; dyMax = 23; dxMin = 12; dxMax = 34;
                         radScale = 0.92f; break;
        case ARCH_SILO:  steps = 6;  dyMin = 17; dyMax = 26; dxMin = 4;  dxMax = 16;
                         radScale = 1.05f; break;
        case ARCH_TWIN:  steps = 12; dyMin = 13; dyMax = 20; dxMin = 8;  dxMax = 24;
                         radScale = 0.85f; turn = 0.12f; break;
        case ARCH_GALLERY: steps = 14; dyMin = 3; dyMax = 12; dxMin = 20; dxMax = 42;
                         radScale = 0.95f; turn = 0.10f; break;
        case ARCH_FORTRESS: steps = 12; radScale = 0.95f; break;
        case ARCH_PENDULUM: steps = 12; dyMin = 17; dyMax = 26; dxMin = 5; dxMax = 20;
                         radScale = 0.85f; break;
        default: steps = 10; radScale = 1.02f; break;  // POWDERKEG
    }

    Vec2 cur = {entranceX_, entranceY_ - 6.0f};
    float bias = rng.f01() < 0.5f ? -1.0f : 1.0f;
    for (int i = 0; i < steps; ++i) {
        path.push_back(cur);
        float rr = spec.tunnelRadius * radScale * rng.range(0.86f, 1.18f);
        // Rooms and necks in alternation: a sequence of separate problems
        // rather than one long corridor. The pinch eases off on early planets.
        if (spec.archetype == ARCH_CHAIN)
            rr *= (i % 2) ? lerpf(1.0f, 0.52f, spec.pinch) : 1.38f;
        if (spec.archetype == ARCH_PENDULUM)
            rr *= ((i % 3) == 1) ? lerpf(1.0f, 0.46f, spec.pinch) : 1.20f;
        radii_.push_back(rr);
        float dy = -rng.range(dyMin, dyMax);
        float dx = bias * rng.range(dxMin, dxMax);
        if (rng.f01() < turn) bias = -bias;
        cur.x = clampf(cur.x + dx, margin, W - margin);
        cur.y = clampf(cur.y + dy, 62.0f, H);
    }

    for (size_t i = 1; i < path.size(); ++i)
        CarveCapsule(path[i - 1], path[i], radii_[i - 1], radii_[i]);
    CarveCapsule(path[0] + Vec2(0, 130.0f), path[0], spec.tunnelRadius * 1.15f, radii_[0]);

    // The second chimney: a faster, meaner way down that rejoins at the bottom.
    if (spec.archetype == ARCH_TWIN && path.size() > 6) {
        Vec2 a = path[2], b = path[path.size() - 2];
        Vec2 prev = a;
        for (int i = 1; i <= 6; ++i) {
            float t = (float)i / 6.0f;
            Vec2 q = lerp(a, b, t) + Vec2(-bias * sinf(t * kPi) * 66.0f, 0);
            q.x = clampf(q.x, margin, W - margin);
            CarveCapsule(prev, q, spec.tunnelRadius * 0.52f, spec.tunnelRadius * 0.52f);
            prev = q;
        }
        CarveCapsule(prev, b, spec.tunnelRadius * 0.52f, radii_[path.size() - 2]);
    }
}

// ----------------------------------------------------------------- rooms

void Field::Rooms(const LevelSpec& spec, Rng& rng) {
    const float W = max_.x;
    const float margin = 52.0f;
    rooms_.clear();
    roomR_.clear();

    auto room = [&](size_t idx, Vec2 offset, float radius) {
        idx = std::min(idx, path.size() - 1);
        Vec2 c = path[idx] + offset;
        c.x = clampf(c.x, margin, W - margin);
        c.y = clampf(c.y, 44.0f, surfaceBase_ - 12.0f);
        CarveCapsule(path[idx], c, radii_[idx] * 0.85f, radius);
        rooms_.push_back(c);
        roomR_.push_back(radius);
    };

    const size_t last = path.size() - 1;
    const size_t mid = path.size() * 55 / 100;
    const bool silo = spec.archetype == ARCH_SILO;
    // Chambers scale with the bore, so an easy planet is roomy throughout
    // rather than being a wide tunnel joining two cramped halls.
    const float rs = spec.tunnelRadius / 17.0f;

    room(mid, Vec2(rng.signed1() * 24.0f, -6.0f), (silo ? 42.0f : 23.0f) * rs);
    room(last, Vec2(rng.signed1() * 28.0f, -4.0f), (silo ? 40.0f : 23.0f) * rs);

    // Side alcoves: shape beyond the corridor, and somewhere to hide a relic.
    int alcoves = 3 + (spec.index % 3);
    for (int i = 0; i < alcoves; ++i) {
        size_t idx = (size_t)rng.rangeI(1, (int)path.size() - 2);
        room(idx, Vec2(rng.signed1() * rng.range(20.0f, 42.0f), rng.range(-14.0f, 14.0f)),
             rng.range(11.0f, 17.0f) * rs);
    }

    // Pillars. In a fortress they ring the objective, which is what turns the
    // approach into the puzzle instead of the shooting.
    if (spec.archetype == ARCH_FORTRESS) {
        Vec2 c = rooms_[0];
        for (int i = 0; i < 8; ++i) {
            float a = kTau * i / 8.0f + rng.f01();
            AddDisc(c + fromAngle(a) * rng.range(15.0f, 21.0f), rng.range(4.0f, 7.0f));
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            Vec2 c = rooms_[i < 3 ? 0 : 1];
            AddDisc(c + fromAngle(rng.f01() * kTau) * rng.range(12.0f, 19.0f),
                    rng.range(3.5f, 6.5f));
        }
    }

    // A gallery needs ledges, or it is just a very wide tunnel.
    if (spec.archetype == ARCH_GALLERY) {
        for (size_t i = 2; i + 1 < path.size(); i += 2) {
            Vec2 c = path[i] + Vec2(rng.signed1() * 8.0f, -radii_[i] * 0.45f);
            for (int k = -2; k <= 2; ++k) AddDisc(c + Vec2(k * 4.5f, 0), 3.2f);
        }
    }
}

// ------------------------------------------------------------- blueprint

void Field::Blueprint(const LevelSpec& spec, Rng& rng) {
    fuelPos.clear();
    fuelNormal.clear();
    relicPos.clear();
    relicNormal.clear();
    turrets.clear();
    minePos.clear();
    dronePos.clear();
    wells.clear();

    startPos = {entranceX_, entranceY_ + 16.0f};
    escapeY = entranceY_ + 78.0f;

    // The orb and the factory are heavy things that were stood on the ground,
    // so they take the floor; everything else takes whichever wall is nearest.
    orbPos = FindFloor(rooms_[1] + Vec2(0, 8.0f));
    SnapToWall(orbPos, 2.0f, &orbPos, &orbNormal);
    reactorPos = FindFloor(rooms_[0] + Vec2(0, 6.0f));
    SnapToWall(reactorPos, 3.6f, &reactorPos, &reactorNormal);

    for (int i = 0; i < spec.fuelCount; ++i) {
        size_t idx = 1 + (size_t)((path.size() - 2) * i / std::max(1, spec.fuelCount - 1));
        idx = std::min(idx, path.size() - 1);
        Vec2 seed;
        if (!FindOpen(rng, path[idx], 16.0f, 3.0f, &seed)) seed = path[idx];
        Vec2 p, n;
        if (!SnapToWall(seed, 1.5f, &p, &n)) continue;
        fuelPos.push_back(p);
        fuelNormal.push_back(n);
    }

    // Relics live in the alcoves, off the fast line. Optional, visible on the
    // planet card, and the reason to fly a planet a fourth time.
    for (size_t i = 2; i < rooms_.size() && (int)relicPos.size() < spec.relicCount; ++i) {
        Vec2 p, n;
        if (!SnapToWall(rooms_[i], 2.0f, &p, &n)) continue;
        relicPos.push_back(p);
        relicNormal.push_back(n);
    }
    while ((int)relicPos.size() < spec.relicCount && !path.empty()) {
        relicPos.push_back(path[path.size() / 2] + Vec2(0, 6.0f));
        relicNormal.push_back(Vec2(0, 1));
    }

    int want = spec.turretCount;
    for (int i = 0; i < want; ++i) {
        size_t idx = 1 + (size_t)((path.size() - 2) * (i + 1) / (want + 1));
        idx = std::min(idx, path.size() - 2);
        Vec2 dir = normalize(path[idx + 1] - path[idx]);
        Vec2 side = perp(dir) * ((i % 2) ? 1.0f : -1.0f);
        Vec2 hit, nrm;
        bool ok = FindWall(path[idx], side, 60.0f, &hit, &nrm);
        if (!ok) ok = FindWall(path[idx], -side, 60.0f, &hit, &nrm);
        if (!ok) continue;
        // Never within sight of the spawn: dying before you have touched the
        // stick is not difficulty.
        if (length(hit - startPos) < 46.0f) continue;
        bool tooClose = false;
        for (auto& t : turrets)
            if (length(t.pos - hit) < 22.0f) tooClose = true;
        if (tooClose) continue;
        turrets.push_back({hit + nrm * 0.6f, nrm});
    }

    const float safeFromStart = 52.0f;
    for (int i = 0; i < spec.mineCount; ++i) {
        for (int attempt = 0; attempt < 6; ++attempt) {
            size_t idx = (size_t)rng.rangeI(1, (int)path.size() - 1);
            Vec2 p;
            if (!FindOpen(rng, path[idx], 26.0f, 4.5f, &p)) continue;
            if (length(p - startPos) < safeFromStart) continue;
            bool crowded = false;
            for (auto& q : minePos)
                if (length(q - p) < 9.0f) crowded = true;
            if (crowded) continue;
            minePos.push_back(p);
            break;
        }
    }

    for (int i = 0; i < spec.droneCount; ++i) {
        size_t idx = 1 + (size_t)((path.size() - 2) * i / std::max(1, spec.droneCount - 1));
        Vec2 p;
        if (!FindOpen(rng, path[idx], 20.0f, 6.0f, &p)) continue;
        if (length(p - startPos) < safeFromStart) continue;
        dronePos.push_back(p);
    }

    int wantWells = spec.wellCount + spec.repulsorCount;
    for (int i = 0; i < wantWells; ++i) {
        bool repel = i >= spec.wellCount;
        size_t idx = 1 + (size_t)((path.size() - 2) * (i + 1) / (wantWells + 1));
        Vec2 p;
        if (!FindOpen(rng, path[idx], 16.0f, 7.0f, &p)) continue;
        if (length(p - startPos) < safeFromStart + 20.0f) continue;
        float radius = rng.range(26.0f, 40.0f) * (0.72f + 0.28f * spec.wellStrength);
        bool crowded = false;
        for (auto& w : wells)
            if (length(w.pos - p) < (w.radius + radius) * 0.55f) crowded = true;
        if (crowded) continue;
        float mag = rng.range(20.0f, 30.0f) * spec.wellStrength;
        wells.push_back({p, repel ? -mag * 0.85f : mag, radius});
    }

    for (auto& m : minePos)
        for (auto& w : wells) {
            Vec2 d = m - w.pos;
            float len = length(d);
            if (len < w.radius * 0.45f && len > 1e-3f) m = w.pos + d * (w.radius * 0.45f / len);
        }
}

// ----------------------------------------------------------------- props

// Every hazard here is also a tool. A tank is a threat, a demolition charge and
// a launch pad depending on where the player is standing when it goes.
void Field::PlaceProps(const LevelSpec& spec, Rng& rng) {
    props.clear();

    auto tooNearStart = [&](Vec2 p) { return length(p - startPos) < 44.0f; };

    auto addTank = [&](Vec2 around, float spread, bool signature) {
        Vec2 base;
        if (!FindOpen(rng, around, spread, 6.0f, &base)) base = around;
        Vec2 floorPt = FindFloor(base);
        PropSpec p;
        p.kind = PROP_TANK;
        p.radius = rng.range(1.9f, 2.7f);
        p.halfW = p.radius * 1.2f;
        p.halfH = p.radius;
        p.pos = floorPt + Vec2(0, p.radius + 2.8f);
        p.anchor = ANCHOR_STRUTS;
        p.anchorPoint = floorPt;
        p.breakImpulse = 22.0f;
        p.density = 0.7f;
        p.signature = signature;
        if (tooNearStart(p.pos) || Sample(p.pos) > -p.radius) return;
        props.push_back(p);
    };

    for (int i = 0; i < spec.tankCount; ++i) {
        size_t idx = 1 + (size_t)((path.size() - 2) * i / std::max(1, spec.tankCount));
        addTank(path[std::min(idx, path.size() - 1)], 24.0f, false);
    }

    for (int i = 0; i < spec.crateCount; ++i) {
        size_t idx = (size_t)rng.rangeI(1, (int)path.size() - 1);
        Vec2 base;
        if (!FindOpen(rng, path[idx], 20.0f, 4.0f, &base)) continue;
        Vec2 floorPt = FindFloor(base);
        PropSpec p;
        p.kind = PROP_CRATE;
        p.halfW = rng.range(1.3f, 1.9f);
        p.halfH = p.halfW * rng.range(0.8f, 1.15f);
        p.pos = floorPt + Vec2(0, p.halfH + 0.4f);
        p.density = 0.55f;
        if (tooNearStart(p.pos)) continue;
        props.push_back(p);
    }

    for (int i = 0; i < spec.boulderCount; ++i) {
        size_t idx = (size_t)rng.rangeI(1, (int)path.size() - 1);
        Vec2 ceil = FindCeiling(path[idx]);
        PropSpec p;
        p.kind = PROP_BOULDER;
        p.radius = rng.range(2.6f, 4.2f);
        p.pos = ceil - Vec2(0, p.radius + 1.4f);
        p.anchor = ANCHOR_PIN;
        p.anchorPoint = ceil;
        p.breakImpulse = rng.range(40.0f, 70.0f);
        p.density = 2.4f;
        if (tooNearStart(p.pos) || Sample(p.pos) > -p.radius) continue;
        props.push_back(p);
    }

    // A shelf is hinged at the wall and propped at the far end. Cut the prop
    // and the whole thing swings down; that is a structure, not a decoration.
    auto addShelf = [&](size_t idx, Vec2 side, float halfW, float breakImpulse, bool signature) {
        Vec2 hit, nrm;
        if (!FindWall(path[idx], side, 50.0f, &hit, &nrm) &&
            !FindWall(path[idx], -side, 50.0f, &hit, &nrm))
            return;
        PropSpec p;
        p.kind = PROP_PLATFORM;
        p.halfW = halfW;
        p.halfH = 0.75f;
        p.pos = hit + nrm * (p.halfW + 0.6f);
        if (tooNearStart(p.pos) || Sample(p.pos) > -1.5f) return;
        Vec2 outer = p.pos + nrm * (p.halfW * 0.85f);
        Vec2 support = FindFloor(outer);
        if (outer.y - support.y > 18.0f || outer.y - support.y < 1.0f) return;
        p.anchor = ANCHOR_SHELF;
        p.anchorPoint = hit + nrm * 1.2f;
        p.anchorPoint2 = support;
        p.breakImpulse = breakImpulse;
        p.density = 0.9f;
        p.signature = signature;
        props.push_back(p);
    };

    for (int i = 0; i < spec.platformCount; ++i) {
        size_t idx = (size_t)rng.rangeI(2, (int)path.size() - 2);
        addShelf(idx, Vec2((i % 2) ? 1.0f : -1.0f, 0), rng.range(7.0f, 11.0f),
                 rng.range(60.0f, 110.0f), false);
    }

    // The signature set piece. Every planet gets exactly one authored moment,
    // because players remember planets by their one moment.
    size_t anchorNode = std::min(path.size() - 2, path.size() * 70 / 100);
    Vec2 stage = path[anchorNode];
    switch (spec.archetype) {
        case ARCH_POWDERKEG:
        case ARCH_CHAIN: {
            for (int i = 0; i < 4; ++i) addTank(stage + Vec2((i - 1.5f) * 7.0f, 0), 6.0f, true);
            break;
        }
        case ARCH_PENDULUM:
        case ARCH_SHAFT: {
            Vec2 ceil = FindCeiling(stage);
            PropSpec p;
            p.kind = PROP_BOULDER;
            p.radius = 4.6f;
            p.pos = ceil - Vec2(0, p.radius + 1.5f);
            p.anchor = ANCHOR_PIN;
            p.anchorPoint = ceil;
            p.breakImpulse = 34.0f;
            p.density = 2.8f;
            p.signature = true;
            if (Sample(p.pos) < -p.radius) props.push_back(p);
            addTank(stage + Vec2(0, -8.0f), 8.0f, true);
            break;
        }
        default: {
            // A propped slab with a charge under it: cut the drum and the whole
            // thing swings out of the way, or down on top of you.
            addShelf(anchorNode, Vec2(1, 0), 9.5f, 55.0f, true);
            addTank(stage, 10.0f, true);
            addTank(stage + Vec2(8.0f, 0), 6.0f, true);
            break;
        }
    }
}

// ------------------------------------------------------------ validation

// Flood fill the open space at the ship's clearance and ask whether the
// objective can actually be flown to. Cheap, and it is the difference between
// "hope the seed is good" and "guaranteed to meet a spec".
bool Field::Reachable(Vec2 from, Vec2 to, float clearance) const {
    auto cellOf = [&](Vec2 p, int* cx, int* cy) {
        *cx = std::max(0, std::min(nx_ - 1, (int)((p.x - min_.x) / cell_ + 0.5f)));
        *cy = std::max(0, std::min(ny_ - 1, (int)((p.y - min_.y) / cell_ + 0.5f)));
    };
    int sx, sy, tx, ty;
    cellOf(from, &sx, &sy);
    cellOf(to, &tx, &ty);

    // The spawn sits in open sky; walk down until the fill has somewhere to go.
    while (sy > 0 && At(sx, sy) > -clearance) --sy;
    if (At(sx, sy) > -clearance) return false;

    const int reachCells = std::max(2, (int)(9.0f / cell_));
    std::vector<uint8_t> seen((size_t)nx_ * ny_, 0);
    std::vector<int> queue;
    queue.reserve(4096);
    queue.push_back(sy * nx_ + sx);
    seen[(size_t)sy * nx_ + sx] = 1;

    for (size_t head = 0; head < queue.size(); ++head) {
        int idx = queue[head];
        int x = idx % nx_, y = idx / nx_;
        if (std::abs(x - tx) <= reachCells && std::abs(y - ty) <= reachCells) return true;
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            int nxx = x + dx[k], nyy = y + dy[k];
            if (nxx < 0 || nyy < 0 || nxx >= nx_ || nyy >= ny_) continue;
            size_t ni = (size_t)nyy * nx_ + nxx;
            if (seen[ni]) continue;
            if (At(nxx, nyy) > -clearance) continue;
            seen[ni] = 1;
            queue.push_back((int)ni);
        }
    }
    return false;
}

// -------------------------------------------------------------- assembly

void Field::Build(const LevelSpec& spec, bool coarse) {
    cell_ = coarse ? 3.0f : 2.0f;
    nx_ = coarse ? 151 : 226;
    ny_ = coarse ? 119 : 178;
    min_ = {0, 0};
    max_ = {(nx_ - 1) * cell_, (ny_ - 1) * cell_};
    v_.assign((size_t)nx_ * ny_, 0.0f);

    Rng rng(spec.seed);
    Terrain(spec);
    Spine(spec, rng);
    Rooms(spec, rng);
    Blueprint(spec, rng);

    // Validation as repair rather than rejection. Widening is monotonic and
    // bounded, so the seed never has to change - and a planet card must always
    // show the planet the player is going to fly.
    const float clearance = 3.4f;
    for (int repair = 1; repair <= 3; ++repair) {
        if (Reachable(startPos, orbPos, clearance) && Reachable(startPos, reactorPos, clearance))
            break;
        float extra = 3.0f * repair;
        for (size_t i = 1; i < path.size(); ++i)
            CarveCapsule(path[i - 1], path[i], radii_[i - 1] + extra, radii_[i] + extra);
        for (size_t i = 0; i < rooms_.size(); ++i) CarveDisc(rooms_[i], roomR_[i] + extra);
        Rng repairRng(spec.seed + 77u * repair);
        Blueprint(spec, repairRng);
    }

    // Carving is finished, so the passage widths are final.
    ComputeRadius();

    if (!coarse) PlaceProps(spec, rng);
}

void Field::Generate(const LevelSpec& spec) { Build(spec, false); }
void Field::GenerateCoarse(const LevelSpec& spec) { Build(spec, true); }

void Field::RasterThumb(uint8_t* out, int w, int h) const {
    // Crop the empty sky: the card should be the cavern, not the half of the
    // map that is nothing but stars.
    const float top = std::min(max_.y, surfaceBase_ + surfAmp_ + 20.0f);
    for (int y = 0; y < h; ++y) {
        float wy = min_.y + (1.0f - (y + 0.5f) / h) * (top - min_.y);
        for (int x = 0; x < w; ++x) {
            float wx = min_.x + (x + 0.5f) / w * (max_.x - min_.x);
            float f = Sample({wx, wy});
            // Two tones: rock reads as a plate, open space as the route.
            float v = f < 0.0f ? clampf(0.60f + (-f) * 0.025f, 0.60f, 1.0f) : 0.16f;
            out[(size_t)y * w + x] = (uint8_t)(v * 255.0f);
        }
    }
}

}  // namespace zp


#include "Physics.h"

#include <cfloat>

#include "Field.h"

namespace zp {

// Tuning. These are the only numbers that decide whether a stack of crates
// settles or jitters, so they live together.
static const float kSlop = 0.02f;
static const float kBaumgarte = 0.22f;
static const float kMaxCorrection = 3.0f;
static const float kRestitutionCut = 2.5f;  // below this an impact is inelastic
static const int kVelIterations = 10;
static const float kSleepLinear = 0.35f;
static const float kSleepAngular = 0.20f;
static const float kSleepDelay = 0.55f;

// ------------------------------------------------------------------ shapes

Shape MakeCircle(float r) {
    Shape s;
    s.kind = SHAPE_CIRCLE;
    s.radius = r;
    s.count = 0;
    s.bound = r;
    return s;
}

Shape MakePoly(const Vec2* pts, int count) {
    Shape s;
    s.kind = SHAPE_POLY;
    s.radius = 0.05f;
    s.count = std::min(count, kMaxVerts);
    float bound = 0;
    for (int i = 0; i < s.count; ++i) {
        s.v[i] = pts[i];
        bound = std::max(bound, length(pts[i]));
    }
    for (int i = 0; i < s.count; ++i) {
        Vec2 e = s.v[(i + 1) % s.count] - s.v[i];
        s.n[i] = normalize(Vec2(e.y, -e.x));
    }
    s.bound = bound + s.radius;
    return s;
}

Shape MakeBox(float hx, float hy) {
    Vec2 p[4] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
    return MakePoly(p, 4);
}

// Area, centroid and second moment of a convex polygon, then recentre so the
// body's origin is its centre of mass - the solver assumes that everywhere.
static void ComputeMass(Body& b, float density) {
    if (b.flags & BF_STATIC) {
        b.mass = b.inertia = b.invMass = b.invI = 0;
        return;
    }
    if (b.shape.kind == SHAPE_CIRCLE) {
        float r = b.shape.radius;
        b.mass = kPi * r * r * density;
        b.inertia = b.mass * 0.5f * r * r;
    } else {
        float area = 0, I = 0;
        Vec2 centroid(0, 0);
        const int n = b.shape.count;
        for (int i = 0; i < n; ++i) {
            Vec2 p1 = b.shape.v[i], p2 = b.shape.v[(i + 1) % n];
            float d = cross(p1, p2);
            float tri = 0.5f * d;
            area += tri;
            centroid += (p1 + p2) * (tri / 3.0f);
            float ix = p1.x * p1.x + p2.x * p1.x + p2.x * p2.x;
            float iy = p1.y * p1.y + p2.y * p1.y + p2.y * p2.y;
            I += (0.25f / 3.0f * d) * (ix + iy);
        }
        if (area < 1e-6f) area = 1e-6f;
        centroid = centroid / area;
        b.mass = density * area;
        b.inertia = density * I - b.mass * dot(centroid, centroid);
        for (int i = 0; i < n; ++i) b.shape.v[i] -= centroid;
        float bound = 0;
        for (int i = 0; i < n; ++i) bound = std::max(bound, length(b.shape.v[i]));
        b.shape.bound = bound + b.shape.radius;
    }
    b.mass = std::max(b.mass, 1e-4f);
    b.inertia = std::max(b.inertia, 1e-4f);
    b.invMass = 1.0f / b.mass;
    b.invI = (b.flags & BF_FIXED_ROT) ? 0.0f : 1.0f / b.inertia;
}

// ------------------------------------------------------------------- world

void World::Reset(const Field* field, Vec2 gravity) {
    field_ = field;
    gravity_ = gravity;
    bodies_.clear();
    joints_.clear();
    contacts_.clear();
    freeBodies_.clear();
    freeJoints_.clear();
    awake_.clear();
    stepCount_ = 0;
}

int World::AddBody(Body b, float density) {
    ComputeMass(b, density);
    b.alive = true;
    b.asleep = false;
    b.sleepTime = 0;
    int slot;
    if (!freeBodies_.empty()) {
        slot = freeBodies_.back();
        freeBodies_.pop_back();
        bodies_[(size_t)slot] = b;
    } else {
        slot = (int)bodies_.size();
        bodies_.push_back(b);
    }
    return slot;
}

void World::RemoveBody(int i) {
    if (!Valid(i)) return;
    bodies_[(size_t)i].alive = false;
    for (auto& j : joints_)
        if (j.alive && (j.a == i || j.b == i)) j.alive = false;
    freeBodies_.push_back(i);
}

int World::AddDistance(int a, int b, Vec2 localA, Vec2 localB, float rest, bool rope,
                       float breakImpulse) {
    Joint j;
    j.kind = JOINT_DISTANCE;
    j.a = a;
    j.b = b;
    j.localA = localA;
    j.localB = localB;
    j.rest = rest;
    j.rope = rope;
    j.breakImpulse = breakImpulse;
    j.alive = true;
    int slot;
    if (!freeJoints_.empty()) {
        slot = freeJoints_.back();
        freeJoints_.pop_back();
        joints_[(size_t)slot] = j;
    } else {
        slot = (int)joints_.size();
        joints_.push_back(j);
    }
    return slot;
}

int World::AddPin(int a, Vec2 worldPoint, float breakImpulse) {
    Joint j;
    j.kind = JOINT_PIN;
    j.a = a;
    j.b = -1;
    j.localA = Valid(a) ? rotate(worldPoint - bodies_[(size_t)a].pos, -bodies_[(size_t)a].ang)
                        : Vec2(0, 0);
    j.localB = worldPoint;
    j.breakImpulse = breakImpulse;
    j.alive = true;
    int slot;
    if (!freeJoints_.empty()) {
        slot = freeJoints_.back();
        freeJoints_.pop_back();
        joints_[(size_t)slot] = j;
    } else {
        slot = (int)joints_.size();
        joints_.push_back(j);
    }
    return slot;
}

void World::RemoveJoint(int j) {
    if (j < 0 || j >= (int)joints_.size() || !joints_[(size_t)j].alive) return;
    joints_[(size_t)j].alive = false;
    freeJoints_.push_back(j);
}

void World::Wake(int i) {
    if (!Valid(i)) return;
    bodies_[(size_t)i].asleep = false;
    bodies_[(size_t)i].sleepTime = 0;
}

void World::ApplyImpulse(int i, Vec2 impulse, Vec2 at) {
    if (!Valid(i)) return;
    Body& b = bodies_[(size_t)i];
    if (b.Static()) return;
    b.vel += impulse * b.invMass;
    b.angVel += cross(at - b.pos, impulse) * b.invI;
    b.asleep = false;
    b.sleepTime = 0;
}

void World::ApplyBlast(Vec2 centre, float radius, float impulse) {
    for (size_t i = 0; i < bodies_.size(); ++i) {
        Body& b = bodies_[i];
        if (!b.alive || b.Static()) continue;
        Vec2 d = b.pos - centre;
        float dist = length(d);
        if (dist > radius) continue;
        if (dist < 1e-4f) {
            d = Vec2(0, 1);
            dist = 1e-4f;
        }
        // Rock eats the pressure wave; only the part of the charge with a clear
        // line to the body pushes it.
        if (field_) {
            Vec2 step = d / dist * 2.5f;
            Vec2 p = centre + step;
            bool blocked = false;
            for (float t = 2.5f; t < dist - 1.0f; t += 2.5f, p += step)
                if (field_->Solid(p)) {
                    blocked = true;
                    break;
                }
            if (blocked) continue;
        }
        float k = 1.0f - dist / radius;
        k = k * k;
        Vec2 push = d / dist * (impulse * k);
        b.vel += push * b.invMass;
        // Off-centre shove so debris tumbles instead of sliding flat.
        b.angVel += cross(d / dist * (b.shape.bound * 0.35f), push) * b.invI * 0.4f;
        b.asleep = false;
        b.sleepTime = 0;
    }
}

// -------------------------------------------------------------- collisions

namespace {

struct WorldPoly {
    Vec2 v[kMaxVerts];
    Vec2 n[kMaxVerts];
    int count = 0;
};

void ToWorld(const Body& b, WorldPoly& out) {
    float c = cosf(b.ang), s = sinf(b.ang);
    out.count = b.shape.count;
    for (int i = 0; i < out.count; ++i) {
        Vec2 p = b.shape.v[i];
        out.v[i] = Vec2(b.pos.x + p.x * c - p.y * s, b.pos.y + p.x * s + p.y * c);
        Vec2 nn = b.shape.n[i];
        out.n[i] = Vec2(nn.x * c - nn.y * s, nn.x * s + nn.y * c);
    }
}

float MaxSeparation(const WorldPoly& A, const WorldPoly& B, int* faceOut) {
    float best = -FLT_MAX;
    int bestFace = 0;
    for (int i = 0; i < A.count; ++i) {
        Vec2 n = A.n[i], p = A.v[i];
        float s = FLT_MAX;
        for (int j = 0; j < B.count; ++j) s = std::min(s, dot(B.v[j] - p, n));
        if (s > best) {
            best = s;
            bestFace = i;
        }
    }
    *faceOut = bestFace;
    return best;
}

int IncidentFace(const WorldPoly& ref, int refFace, const WorldPoly& inc) {
    Vec2 n = ref.n[refFace];
    int best = 0;
    float bestDot = FLT_MAX;
    for (int i = 0; i < inc.count; ++i) {
        float d = dot(inc.n[i], n);
        if (d < bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

// Clips segment [a,b] against the half space dot(p - origin, n) <= offset.
int ClipSegment(Vec2 n, float offset, Vec2 in0, Vec2 in1, Vec2* out) {
    int count = 0;
    float d0 = dot(n, in0) - offset;
    float d1 = dot(n, in1) - offset;
    if (d0 <= 0) out[count++] = in0;
    if (d1 <= 0) out[count++] = in1;
    if (d0 * d1 < 0 && count < 2) out[count++] = lerp(in0, in1, d0 / (d0 - d1));
    return count;
}

Vec2 ClosestOnSegment(Vec2 a, Vec2 b, Vec2 p) {
    Vec2 ab = b - a;
    float l2 = lengthSq(ab);
    if (l2 < 1e-8f) return a;
    return a + ab * clampf(dot(p - a, ab) / l2, 0.0f, 1.0f);
}

}  // namespace

void World::CollidePair(int ia, int ib) {
    Body& A = bodies_[(size_t)ia];
    Body& B = bodies_[(size_t)ib];

    float rest = std::min(A.restitution, B.restitution);
    float fric = sqrtf(A.friction * B.friction);

    auto push = [&](Vec2 p, Vec2 n, float depth) {
        if (depth <= 0) return;
        Contact c;
        c.a = ia;
        c.b = ib;
        c.p = p;
        c.n = n;  // points from A toward B
        c.depth = depth;
        c.restitution = rest;
        c.friction = fric;
        contacts_.push_back(c);
    };

    if (A.shape.kind == SHAPE_CIRCLE && B.shape.kind == SHAPE_CIRCLE) {
        Vec2 d = B.pos - A.pos;
        float dist = length(d);
        float r = A.shape.radius + B.shape.radius;
        if (dist >= r) return;
        Vec2 n = dist > 1e-5f ? d / dist : Vec2(0, 1);
        push(A.pos + n * A.shape.radius, n, r - dist);
        return;
    }

    if (A.shape.kind == SHAPE_CIRCLE || B.shape.kind == SHAPE_CIRCLE) {
        bool circleIsA = A.shape.kind == SHAPE_CIRCLE;
        const Body& C = circleIsA ? A : B;
        const Body& P = circleIsA ? B : A;
        WorldPoly wp;
        ToWorld(P, wp);
        // Deepest face relative to the circle centre.
        float best = -FLT_MAX;
        int face = 0;
        for (int i = 0; i < wp.count; ++i) {
            float s = dot(C.pos - wp.v[i], wp.n[i]);
            if (s > best) {
                best = s;
                face = i;
            }
        }
        if (best > C.shape.radius) return;
        Vec2 n, point;
        float depth;
        if (best < 1e-4f) {
            n = wp.n[face];
            point = C.pos - n * C.shape.radius;
            depth = C.shape.radius - best;
        } else {
            Vec2 a = wp.v[face], b = wp.v[(face + 1) % wp.count];
            Vec2 q = ClosestOnSegment(a, b, C.pos);
            Vec2 d = C.pos - q;
            float dist = length(d);
            if (dist > C.shape.radius) return;
            n = dist > 1e-5f ? d / dist : wp.n[face];
            point = q;
            depth = C.shape.radius - dist;
        }
        // Normal must always run from A to B.
        if (circleIsA) n = -n;
        push(point, n, depth);
        return;
    }

    WorldPoly wa, wb;
    ToWorld(A, wa);
    ToWorld(B, wb);
    int faceA = 0, faceB = 0;
    float sepA = MaxSeparation(wa, wb, &faceA);
    if (sepA > 0) return;
    float sepB = MaxSeparation(wb, wa, &faceB);
    if (sepB > 0) return;

    bool flip = sepB > sepA + 0.001f;
    const WorldPoly& ref = flip ? wb : wa;
    const WorldPoly& inc = flip ? wa : wb;
    int refFace = flip ? faceB : faceA;

    Vec2 refN = ref.n[refFace];
    Vec2 r1 = ref.v[refFace], r2 = ref.v[(refFace + 1) % ref.count];
    Vec2 tangent = normalize(r2 - r1);

    int incFace = IncidentFace(ref, refFace, inc);
    Vec2 i1 = inc.v[incFace], i2 = inc.v[(incFace + 1) % inc.count];

    Vec2 clip[2];
    if (ClipSegment(-tangent, dot(-tangent, r1), i1, i2, clip) < 2) return;
    Vec2 clip2[2];
    if (ClipSegment(tangent, dot(tangent, r2), clip[0], clip[1], clip2) < 2) return;

    float refOffset = dot(refN, r1);
    for (int i = 0; i < 2; ++i) {
        float sep = dot(refN, clip2[i]) - refOffset;
        if (sep > 0) continue;
        Vec2 n = flip ? -refN : refN;
        push(clip2[i], n, -sep);
    }
}

// Terrain is the signed field, so a body only has to ask "how deep am I and
// which way is out" at a handful of sample points.
void World::CollideTerrain(int i) {
    Body& b = bodies_[(size_t)i];
    if (!field_) return;

    auto probe = [&](Vec2 world, float radius) {
        Vec2 n;
        float depth;
        if (!field_->SphereHit(world, radius, &n, &depth)) return;
        if (depth <= 0) return;
        Contact c;
        c.a = i;
        c.b = -1;
        c.p = world - n * radius;
        c.n = -n;  // from body into rock, matching the A -> B convention
        c.depth = depth;
        c.restitution = b.restitution;
        c.friction = std::max(b.friction, 0.5f);
        contacts_.push_back(c);
    };

    if (b.shape.kind == SHAPE_CIRCLE) {
        probe(b.pos, b.shape.radius);
        return;
    }
    const int n = b.shape.count;
    for (int k = 0; k < n; ++k) {
        Vec2 a = b.ToWorld(b.shape.v[k]);
        probe(a, b.shape.radius);
        // Long edges also get their midpoint, or a wide crate can straddle a
        // spike with both ends in open air.
        Vec2 c = b.ToWorld(b.shape.v[(k + 1) % n]);
        if (lengthSq(c - a) > 16.0f) probe((a + c) * 0.5f, b.shape.radius);
    }
}

void World::BuildContacts() {
    contacts_.clear();
    const int n = (int)bodies_.size();
    for (int i = 0; i < n; ++i) {
        Body& b = bodies_[(size_t)i];
        if (!b.alive || b.asleep) continue;
        if (!b.Static() && !(b.flags & BF_NO_TERRAIN)) CollideTerrain(i);
    }
    for (int i = 0; i < n; ++i) {
        const Body& A = bodies_[(size_t)i];
        if (!A.alive) continue;
        for (int j = i + 1; j < n; ++j) {
            const Body& B = bodies_[(size_t)j];
            if (!B.alive) continue;
            if (A.Static() && B.Static()) continue;
            if (A.asleep && B.asleep) continue;
            if (A.Static() && B.asleep) continue;
            if (B.Static() && A.asleep) continue;
            float r = A.shape.bound + B.shape.bound;
            if (lengthSq(B.pos - A.pos) > r * r) continue;
            CollidePair(i, j);
        }
    }
}

void World::PrepareContacts(float dt) {
    float invDt = dt > 0 ? 1.0f / dt : 0.0f;
    for (auto& c : contacts_) {
        Body& A = bodies_[(size_t)c.a];
        Vec2 ra = c.p - A.pos;
        float invMassSum = A.invMass;
        float rnA = cross(ra, c.n);
        float rtA = cross(ra, perp(c.n));
        float kn = invMassSum + rnA * rnA * A.invI;
        float kt = invMassSum + rtA * rtA * A.invI;
        Vec2 relVel = A.VelAt(c.p);

        if (c.b >= 0) {
            Body& B = bodies_[(size_t)c.b];
            Vec2 rb = c.p - B.pos;
            float rnB = cross(rb, c.n);
            float rtB = cross(rb, perp(c.n));
            kn += B.invMass + rnB * rnB * B.invI;
            kt += B.invMass + rtB * rtB * B.invI;
            relVel = B.VelAt(c.p) - relVel;
        } else {
            relVel = -relVel;
        }

        c.massN = kn > 1e-8f ? 1.0f / kn : 0.0f;
        c.massT = kt > 1e-8f ? 1.0f / kt : 0.0f;
        c.relN = dot(relVel, c.n);
        c.bias = -kBaumgarte * invDt * std::min(0.0f, -std::max(0.0f, c.depth - kSlop));
        c.bias = clampf(c.bias, 0.0f, kMaxCorrection * 60.0f * dt * invDt);
        // Slow contacts must not bounce, or nothing ever comes to rest.
        if (c.relN > -kRestitutionCut) c.restitution = 0.0f;
        c.impN = 0;
        c.impT = 0;
    }
}

void World::SolveContacts() {
    for (auto& c : contacts_) {
        Body& A = bodies_[(size_t)c.a];
        Body* B = c.b >= 0 ? &bodies_[(size_t)c.b] : nullptr;

        Vec2 va = A.VelAt(c.p);
        Vec2 rel = B ? (B->VelAt(c.p) - va) : -va;

        float vn = dot(rel, c.n);
        float target = -c.restitution * c.relN;
        float lambda = -c.massN * (vn - target - c.bias);
        float old = c.impN;
        c.impN = std::max(0.0f, old + lambda);
        lambda = c.impN - old;

        Vec2 P = c.n * lambda;
        if (!A.Static()) {
            A.vel -= P * A.invMass;
            A.angVel -= cross(c.p - A.pos, P) * A.invI;
        }
        if (B && !B->Static()) {
            B->vel += P * B->invMass;
            B->angVel += cross(c.p - B->pos, P) * B->invI;
        }

        // Friction, clamped to the Coulomb cone of the normal impulse so far.
        va = A.VelAt(c.p);
        rel = B ? (B->VelAt(c.p) - va) : -va;
        Vec2 t = perp(c.n);
        float vt = dot(rel, t);
        float lt = -c.massT * vt;
        float maxT = c.friction * c.impN;
        float oldT = c.impT;
        c.impT = clampf(oldT + lt, -maxT, maxT);
        lt = c.impT - oldT;

        Vec2 Pt = t * lt;
        if (!A.Static()) {
            A.vel -= Pt * A.invMass;
            A.angVel -= cross(c.p - A.pos, Pt) * A.invI;
        }
        if (B && !B->Static()) {
            B->vel += Pt * B->invMass;
            B->angVel += cross(c.p - B->pos, Pt) * B->invI;
        }
    }
}

void World::SolveJoints(float dt) {
    float invDt = dt > 0 ? 1.0f / dt : 0.0f;
    for (auto& j : joints_) {
        if (!j.alive) continue;
        if (!Valid(j.a)) {
            j.alive = false;
            continue;
        }
        Body& A = bodies_[(size_t)j.a];
        Body* B = (j.b >= 0 && Valid(j.b)) ? &bodies_[(size_t)j.b] : nullptr;
        if (j.b >= 0 && !B) {
            j.alive = false;
            continue;
        }

        if (j.kind == JOINT_PIN) {
            // Two axis point constraint against a fixed world anchor.
            Vec2 pa = A.ToWorld(j.localA);
            Vec2 target = j.localB;
            Vec2 ra = pa - A.pos;
            Vec2 cvel = A.VelAt(pa);
            Vec2 err = pa - target;

            float rx = ra.x, ry = ra.y;
            float k11 = A.invMass + ry * ry * A.invI;
            float k12 = -rx * ry * A.invI;
            float k22 = A.invMass + rx * rx * A.invI;
            float det = k11 * k22 - k12 * k12;
            if (fabsf(det) < 1e-9f) continue;
            Vec2 rhs = -(cvel + err * (kBaumgarte * invDt));
            float ix = (k22 * rhs.x - k12 * rhs.y) / det;
            float iy = (k11 * rhs.y - k12 * rhs.x) / det;
            Vec2 P(ix, iy);
            A.vel += P * A.invMass;
            A.angVel += cross(ra, P) * A.invI;
            j.accum += P;
            j.load = length(j.accum);
            if (j.breakImpulse > 0 && j.load > j.breakImpulse) {
                j.alive = false;
                j.justBroke = true;
                A.asleep = false;
            }
            continue;
        }

        Vec2 pa = A.ToWorld(j.localA);
        Vec2 pb = B ? B->ToWorld(j.localB) : j.localB;
        Vec2 d = pb - pa;
        float len = length(d);
        if (len < 1e-5f) continue;
        Vec2 n = d / len;
        float c = len - j.rest;
        if (j.rope && c < 0) {
            j.load = 0;
            continue;
        }

        Vec2 ra = pa - A.pos;
        float rnA = cross(ra, n);
        float k = A.invMass + rnA * rnA * A.invI;
        Vec2 relVel = A.VelAt(pa);
        if (B) {
            Vec2 rb = pb - B->pos;
            float rnB = cross(rb, n);
            k += B->invMass + rnB * rnB * B->invI;
            relVel = B->VelAt(pb) - relVel;
        } else {
            relVel = -relVel;
        }
        if (k < 1e-8f) continue;

        float bias = kBaumgarte * invDt * c;
        float lambda = -(dot(relVel, n) + bias) / k;
        if (j.rope) lambda = std::min(lambda, 0.0f);
        j.accumScalar += lambda;
        j.load = fabsf(j.accumScalar);

        Vec2 P = n * lambda;
        A.vel -= P * A.invMass;
        A.angVel -= cross(ra, P) * A.invI;
        if (B) {
            B->vel += P * B->invMass;
            B->angVel += cross(pb - B->pos, P) * B->invI;
        }

        if (j.breakImpulse > 0 && j.load > j.breakImpulse) {
            j.alive = false;
            j.justBroke = true;
            A.asleep = false;
            if (B) B->asleep = false;
        }
    }
}

void World::IntegrateVelocities(float dt) {
    for (auto& b : bodies_) {
        if (!b.alive || b.Static() || b.asleep) continue;
        Vec2 a = b.force * b.invMass;
        if (!(b.flags & BF_NO_GRAVITY)) a += gravity_ * b.gravityScale;
        b.vel += a * dt;
        b.angVel += b.torque * b.invI * dt;
        b.vel *= 1.0f - damp(b.linDamp, dt);
        b.angVel *= 1.0f - damp(b.angDamp, dt);
        b.force = {0, 0};
        b.torque = 0;
    }
}

void World::IntegratePositions(float dt) {
    for (auto& b : bodies_) {
        if (!b.alive || b.Static() || b.asleep) continue;
        b.pos += b.vel * dt;
        b.ang += b.angVel * dt;
    }
}

void World::UpdateSleep(float dt) {
    for (auto& b : bodies_) {
        if (!b.alive || b.Static()) continue;
        if (b.flags & BF_NO_SLEEP) {
            b.asleep = false;
            continue;
        }
        if (length(b.vel) > kSleepLinear || fabsf(b.angVel) > kSleepAngular) {
            b.sleepTime = 0;
            b.asleep = false;
        } else {
            b.sleepTime += dt;
            if (b.sleepTime > kSleepDelay) {
                b.asleep = true;
                b.vel = {0, 0};
                b.angVel = 0;
            }
        }
    }
    // A sleeping body still jointed to something moving has to come back.
    for (auto& j : joints_) {
        if (!j.alive) continue;
        bool aAwake = Valid(j.a) && !bodies_[(size_t)j.a].asleep && !bodies_[(size_t)j.a].Static();
        bool bAwake = j.b >= 0 && Valid(j.b) && !bodies_[(size_t)j.b].asleep &&
                      !bodies_[(size_t)j.b].Static();
        if (aAwake && j.b >= 0) Wake(j.b);
        if (bAwake) Wake(j.a);
    }
}

void World::ReportImpacts() {
    for (auto& b : bodies_) b.impact = 0;
    for (auto& c : contacts_) {
        if (c.impN <= 0) continue;
        Body& A = bodies_[(size_t)c.a];
        if (c.impN > A.impact) {
            A.impact = c.impN;
            A.impactPos = c.p;
            A.impactNormal = -c.n;
        }
        if (c.b >= 0) {
            Body& B = bodies_[(size_t)c.b];
            if (c.impN > B.impact) {
                B.impact = c.impN;
                B.impactPos = c.p;
                B.impactNormal = c.n;
            }
        }
    }
}

void World::Step(float dt) {
    ++stepCount_;
    for (auto& j : joints_) {
        j.accum = {0, 0};
        j.accumScalar = 0;
        j.justBroke = false;
    }
    IntegrateVelocities(dt);
    BuildContacts();
    PrepareContacts(dt);
    for (int it = 0; it < kVelIterations; ++it) {
        SolveJoints(dt);
        SolveContacts();
    }
    IntegratePositions(dt);
    ReportImpacts();
    UpdateSleep(dt);
}

}  // namespace zp

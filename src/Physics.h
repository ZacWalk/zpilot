// z-pilot - compact 2D rigid body engine. Circles and convex polygons collide
// with each other and with the cavern's signed field, held together by joints
// that can be torn apart. Everything runs on a fixed step in a fixed order, so
// a given seed plus a given input trace always plays out the same way.
#pragma once
#include "Common.h"

namespace zp {

class Field;

constexpr int kMaxVerts = 8;

enum ShapeKind : int { SHAPE_CIRCLE = 0, SHAPE_POLY = 1 };

struct Shape {
    int kind = SHAPE_CIRCLE;
    float radius = 1.0f;  // circle radius, or the contact skin of a polygon
    int count = 0;
    Vec2 v[kMaxVerts] = {};
    Vec2 n[kMaxVerts] = {};  // outward normal of edge v[i] -> v[i+1]
    float bound = 1.0f;      // radius of the bounding circle, for broad phase
};

Shape MakeCircle(float r);
Shape MakeBox(float halfW, float halfH);
Shape MakePoly(const Vec2* pts, int count);  // counter clockwise

enum BodyFlag : uint32_t {
    BF_STATIC = 1u << 0,
    BF_NO_TERRAIN = 1u << 1,
    BF_NO_GRAVITY = 1u << 2,
    BF_NO_SLEEP = 1u << 3,
    BF_FIXED_ROT = 1u << 4,
};

struct Body {
    Vec2 pos = {}, vel = {};
    float ang = 0, angVel = 0;
    Shape shape;

    float mass = 1, invMass = 1, inertia = 1, invI = 1;
    float restitution = 0.16f, friction = 0.55f;
    float linDamp = 0.04f, angDamp = 0.35f;
    float gravityScale = 1.0f;
    uint32_t flags = 0;

    bool alive = false;
    bool asleep = false;
    float sleepTime = 0;

    Vec2 force = {};
    float torque = 0;

    // Largest contact impulse seen during the last step. The game reads this for
    // shield cost, damage, dust and audio, so impacts never need a special case.
    float impact = 0;
    Vec2 impactPos = {}, impactNormal = {};

    bool Static() const { return (flags & BF_STATIC) != 0; }
    Vec2 ToWorld(Vec2 local) const { return pos + rotate(local, ang); }
    Vec2 VelAt(Vec2 world) const {
        Vec2 r = world - pos;
        return vel + Vec2(-angVel * r.y, angVel * r.x);
    }
};

enum JointKind : int { JOINT_DISTANCE = 0, JOINT_PIN = 1 };

struct Joint {
    int kind = JOINT_DISTANCE;
    int a = -1, b = -1;  // b < 0 anchors to the world
    Vec2 localA = {}, localB = {};
    float rest = 0;
    bool rope = false;       // distance joints that only resist stretching
    float breakImpulse = 0;  // 0 means it never breaks
    bool alive = false;
    bool justBroke = false;
    float load = 0;  // impulse carried last step, for creaking visuals
    Vec2 accum = {};
    float accumScalar = 0;
};

class World {
public:
    void Reset(const Field* field, Vec2 gravity);
    void SetGravity(Vec2 g) { gravity_ = g; }
    Vec2 Gravity() const { return gravity_; }

    int AddBody(Body b, float density = 1.0f);
    void RemoveBody(int i);
    Body& operator[](int i) { return bodies_[(size_t)i]; }
    const Body& operator[](int i) const { return bodies_[(size_t)i]; }
    int BodyCount() const { return (int)bodies_.size(); }
    bool Valid(int i) const {
        return i >= 0 && i < (int)bodies_.size() && bodies_[(size_t)i].alive;
    }

    int AddDistance(int a, int b, Vec2 localA, Vec2 localB, float rest, bool rope,
                    float breakImpulse);
    int AddPin(int a, Vec2 worldPoint, float breakImpulse);
    void RemoveJoint(int j);
    Joint& JointAt(int j) { return joints_[(size_t)j]; }
    const Joint& JointAt(int j) const { return joints_[(size_t)j]; }
    int JointCount() const { return (int)joints_.size(); }

    void Step(float dt);

    void ApplyImpulse(int i, Vec2 impulse, Vec2 at);
    void ApplyForce(int i, Vec2 f) {
        if (Valid(i)) bodies_[(size_t)i].force += f;
    }
    // Radial shove with a smooth falloff. Blocked by rock, so a charge set off
    // around a corner shakes the walls and nothing else.
    void ApplyBlast(Vec2 centre, float radius, float impulse);
    void Wake(int i);

private:
    struct Contact {
        int a = -1, b = -1;  // b < 0 means terrain
        Vec2 p = {}, n = {};
        float depth = 0;
        float massN = 0, massT = 0, bias = 0, relN = 0;
        float restitution = 0, friction = 0;
        float impN = 0, impT = 0;
    };

    void IntegrateVelocities(float dt);
    void BuildContacts();
    void CollidePair(int ia, int ib);
    void CollideTerrain(int i);
    void PrepareContacts(float dt);
    void SolveContacts();
    void SolveJoints(float dt);
    void IntegratePositions(float dt);
    void UpdateSleep(float dt);
    void ReportImpacts();

    std::vector<Body> bodies_;
    std::vector<Joint> joints_;
    std::vector<Contact> contacts_;
    std::vector<int> freeBodies_, freeJoints_;

    const Field* field_ = nullptr;
    Vec2 gravity_ = {0, -6.0f};
};

}  // namespace zp

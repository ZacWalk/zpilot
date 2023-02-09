// z-pilot - procedural planet: a signed field (positive = rock) carved to an
// archetype, plus the blueprint of everything that hangs, floats or is bolted
// inside it. One planet index maps to exactly one cavern, forever.
#pragma once
#include "Common.h"

namespace zp {

// Bump whenever generation changes shape. Saved scores refer to caverns built
// by a specific generator, so the profile is discarded when this moves.
constexpr uint32_t kGenVersion = 6;

enum Archetype : int {
    ARCH_SHAFT = 0,   // narrow vertical drop, precision braking
    ARCH_CHAIN,       // wide rooms joined by tight necks
    ARCH_SILO,        // one big chamber, everything visible at once
    ARCH_TWIN,        // two routes down, one fast and one safe
    ARCH_GALLERY,     // wide horizontal sweep with ledges
    ARCH_FORTRESS,    // a defended objective, the puzzle is the approach
    ARCH_PENDULUM,    // built around swinging the orb through chokepoints
    ARCH_POWDERKEG,   // dense explosives, the chain reaction is the solution
    ARCH_COUNT
};

enum PropKind : int {
    PROP_TANK = 0,   // volatile drum: shoot it and everything nearby moves
    PROP_CRATE,      // inert, pushable, stacks
    PROP_BOULDER,    // heavy, usually hanging until something jars it loose
    PROP_STRUT,      // the frame a tank sits on
    PROP_PLATFORM,   // long slab on a hinge - topples or seesaws
    PROP_KIND_COUNT
};

enum PropAnchor : int {
    ANCHOR_FREE = 0,  // loose, gravity only
    ANCHOR_PIN,       // pinned to the rock, can be torn off
    ANCHOR_STRUTS,    // held up by breakable legs
    ANCHOR_SHELF,     // hinged at the wall, propped at the far end
};

struct PropSpec {
    int kind = PROP_CRATE;
    int anchor = ANCHOR_FREE;
    Vec2 pos = {};
    Vec2 anchorPoint = {};
    Vec2 anchorPoint2 = {};  // shelves: where the outer prop meets rock
    float ang = 0;
    float halfW = 1.0f, halfH = 1.0f;  // boxes
    float radius = 1.0f;               // circles
    float breakImpulse = 0;
    float density = 1.0f;
    bool signature = false;  // part of this planet's one authored moment
};

struct LevelSpec {
    int index = 0;
    uint32_t seed = 1;
    int archetype = ARCH_SHAFT;
    int row = 0, col = 0;

    float gravity = 5.4f;
    float darkness = 0.0f;   // 1 = world only visible near the ship

    int turretCount = 3;
    int mineCount = 0;
    int droneCount = 0;
    int wellCount = 0;      // attractors
    int repulsorCount = 0;
    int fuelCount = 4;
    int tankCount = 0;
    int crateCount = 0;
    int boulderCount = 0;
    int platformCount = 0;
    int relicCount = 0;
    int reactorHits = 3;

    // Hazards arrive weak and grow into themselves: 0 means absent, otherwise
    // 0.45 on the planet that introduces them rising to 1 several planets later.
    float mineStrength = 0;
    float droneStrength = 0;
    float wellStrength = 0;
    // How hard the chain and pendulum archetypes are allowed to pinch their
    // necks. Low early, so a tight squeeze is a late-game problem.
    float pinch = 1.0f;

    float tunnelRadius = 15.0f;
    float bulletSpeed = 26.0f;
    float fireInterval = 2.6f;

    const char* archetypeName = "DESCENT SHAFT";
    char name[20] = "";

    static LevelSpec For(int index);
};

struct TurretSite {
    Vec2 pos;
    Vec2 normal;
};

// Signed strength: positive pulls in, negative shoves away.
struct WellSite {
    Vec2 pos;
    float strength = 0;
    float radius = 34.0f;
};

class Field {
public:
    void Generate(const LevelSpec& spec);
    // Quarter resolution pass used only to draw the planet cards. Same seed and
    // same carving, so the card really is the cavern.
    void GenerateCoarse(const LevelSpec& spec);

    float Sample(Vec2 p) const;
    Vec2 Gradient(Vec2 p) const;
    bool Solid(Vec2 p) const { return Sample(p) > 0.0f; }
    // Cheap sphere cast used by ship/pod/bullet collision.
    bool SphereHit(Vec2 p, float radius, Vec2* normalOut, float* depthOut) const;

    float At(int x, int y) const;
    float AtBase(int x, int y) const;  // terrain before the tunnels were carved
    // Radius of the largest passage the point sits inside. This is what lets
    // the cave be modelled as a half pipe instead of a bowl.
    float RadiusAt(int x, int y) const;
    // Projects a point onto the nearest cave wall and returns the outward
    // normal, so anything mounted reads as bolted to the 2D boundary.
    bool SnapToWall(Vec2 from, float standoff, Vec2* pos, Vec2* normal) const;
    int NX() const { return nx_; }
    int NY() const { return ny_; }
    float Cell() const { return cell_; }
    Vec2 Min() const { return min_; }
    Vec2 Max() const { return max_; }
    Vec2 CellCentre(int x, int y) const { return {min_.x + x * cell_, min_.y + y * cell_}; }

    // 1 where the cavern is open, 0 in rock. Drawn on the planet cards.
    void RasterThumb(uint8_t* out, int w, int h) const;

    // Blueprint results.
    Vec2 startPos = {};
    Vec2 orbPos = {};
    Vec2 reactorPos = {};
    float escapeY = 0;

    std::vector<Vec2> fuelPos;
    std::vector<Vec2> fuelNormal;
    std::vector<Vec2> relicPos;
    std::vector<Vec2> relicNormal;
    Vec2 orbNormal = {0, 1};
    Vec2 reactorNormal = {0, 1};
    std::vector<TurretSite> turrets;
    std::vector<Vec2> minePos;
    std::vector<Vec2> dronePos;
    std::vector<WellSite> wells;
    std::vector<PropSpec> props;
    std::vector<Vec2> path;

private:
    void Build(const LevelSpec& spec, bool coarse);
    void Terrain(const LevelSpec& spec);
    void Spine(const LevelSpec& spec, Rng& rng);
    void Rooms(const LevelSpec& spec, Rng& rng);
    void Blueprint(const LevelSpec& spec, Rng& rng);
    void PlaceProps(const LevelSpec& spec, Rng& rng);
    bool Reachable(Vec2 from, Vec2 to, float clearance) const;

    void CarveCapsule(Vec2 a, Vec2 b, float r0, float r1);
    void CarveDisc(Vec2 c, float r);
    void AddDisc(Vec2 c, float r);
    void ComputeRadius();
    Vec2 FindFloor(Vec2 from) const;
    Vec2 FindCeiling(Vec2 from) const;
    bool FindWall(Vec2 from, Vec2 dir, float maxDist, Vec2* hit, Vec2* normal) const;
    // Open air with at least `clearance` units of rock standoff.
    bool FindOpen(Rng& rng, Vec2 around, float spread, float clearance, Vec2* out) const;

    int nx_ = 0, ny_ = 0;
    float cell_ = 2.0f;
    Vec2 min_ = {}, max_ = {};
    std::vector<float> v_;
    std::vector<float> base_;
    std::vector<float> passage_;  // largest inscribed passage radius per cell
    std::vector<float> radii_;    // spine node radii, used by the repair pass
    std::vector<Vec2> rooms_;
    std::vector<float> roomR_;
    float surfaceBase_ = 0, surfAmp_ = 0;
    float entranceX_ = 0, entranceY_ = 0;
};

const char* ArchetypeName(int archetype);
const char* MedalName(int medal);

}  // namespace zp

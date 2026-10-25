// zpilot - gameplay: flight model, rigid body props, turrets, scoring rubric,
// particle systems, planet select and HUD.
#pragma once
#include "Field.h"
#include "Font.h"
#include "Geometry.h"
#include "Physics.h"
#include "Profile.h"
#include "Renderer.h"
#include "Synth.h"
#include "Window.h"

namespace zp {

enum class Mode { Title, Grid, Briefing, Playing, Dying, Results };

struct Particle {
    Vec2 p, v;
    float life = 0, maxLife = 1;
    float size = 1, growth = 0;
    Vec3 col0, col1;
    float drag = 1.0f;
    float gravity = 0.0f;
    float z = 0;
    float stretch = 0;
    int type = 0;
    bool collide = false;
};

struct Shard {
    Vec2 p, v;
    Vec4 q = {0, 0, 0, 1};
    Vec3 axis = {0, 0, 1};
    float spin = 1;
    float life = 0, maxLife = 1;
    float size = 1;
    Vec3 col;
};

struct Bullet {
    Vec2 p, v;
    float life = 0;
    bool player = true;
    float hue = 0;
};

struct Turret {
    Vec2 pos, normal;
    float baseAngle = 0;
    float aim = 0;
    float cooldown = 1.0f;
    bool alive = true;
    bool tracking = false;  // has line of sight and is not stunned
};

struct FuelCell {
    Vec2 p;
    bool alive = true;
    float phase = 0;
};

// Neutrally buoyant proximity mine. Arms on approach, then a fixed fuse - once
// it is ticking, running is the only answer.
struct Mine {
    Vec2 p, v;
    bool alive = true;
    float fuse = 0;     // >0 once armed
    float phase = 0;
    float spin = 0;
};

// Rammer. Patrols its spawn until it has line of sight, then commits.
struct Drone {
    Vec2 p, v, home;
    float ang = 0;
    float hp = 2;
    bool alive = true;
    bool hunting = false;
    float wander = 0;
    float thrust = 0;
};

// XPilot style field emitter. Signed strength: positive sucks, negative blows.
struct Well {
    Vec2 pos;
    float strength = 0;
    float radius = 34;
    float hp = 3;
    bool alive = true;
    float flare = 0;
};

struct Star {
    Vec3 p;
    float size;
    Vec3 col;
};

// A rigid body the generator bolted, hung or dropped into the cavern. Volatile
// ones are as useful to the player as they are dangerous.
struct Prop {
    int kind = PROP_CRATE;
    int body = -1;
    int jointA = -1, jointB = -1;
    Vec2 anchorA = {}, anchorB = {};
    float hp = 3;
    float fuse = -1;  // >= 0 once a nearby blast has lit it
    bool alive = true;
    bool signature = false;
    float flash = 0;
    float halfW = 1, halfH = 1, radius = 1;
};

// Optional pickup tucked into a side alcove: the reason to fly a planet again
// once it is already cleared.
struct Relic {
    Vec2 p;
    bool taken = false;
    float phase = 0;
};

// Everything the score rubric needs. Filled in during the run, graded at the
// exit, and shown line by line so a retry has a target.
struct RunStats {
    float shieldSpent = 0;
    float fuelLeft = 0;
    bool orbReached = false;
    bool orbExtracted = false;
    bool reactorKilled = false;
    bool completed = false;
    bool lost = false;  // the ship was destroyed, as opposed to flying home empty
    int relics = 0;
    int objectiveScore = 0, fuelScore = 0, integrityScore = 0, relicScore = 0;
    int percent = 0;
    int medal = 0;
    bool best = false;
};

constexpr int kThumbW = 32;
constexpr int kThumbH = 20;

class Game {
public:
    bool Init(Renderer* r, Synth* s, Window* w);
    void Frame(float dt);
    bool WantsQuit() const { return quit_; }
    Mode CurrentMode() const { return mode_; }  // the scripted capture checks its shots against these
    int CurrentPlanet() const { return spec_.index; }

private:
    void StartPlanet(int index);
    void BuildThumbnails();
    void Simulate(float dt);
    void UpdateShip(float dt);
    void UpdateRope(float dt);
    void UpdateProps(float dt);
    void UpdateBullets(float dt);
    void UpdateTurrets(float dt);
    void UpdateMines(float dt);
    void UpdateDrones(float dt);
    void UpdateParticles(float dt);
    void UpdateCamera(float dt, float realDt);
    void UpdateAudio();
    void UpdateGrid();

    void Render();
    // Submits this frame's lamps. Must run before BeginScene, which folds them
    // into the frame constants.
    void GatherLights();
    void RenderWorld();
    void RenderEffects();
    void RenderHud();
    void RenderTitle();
    void RenderGrid();
    void RenderResults();

    void Emit(Vec2 p, Vec2 v, int count, Vec3 c0, Vec3 c1, float sizeMin, float sizeMax,
              float lifeMin, float lifeMax, float spread, float speed, bool collide, int type = 0);
    void Explode(Vec2 p, float scale, Vec3 col, bool big);
    void AddShockwave(Vec2 world, float strength);
    // The one route damage takes. `hard` marks impacts heavy enough to knock the
    // emitter offline for a moment.
    void ShieldHit(float cost, Vec2 at, bool hard);
    void KillShip(const char* reason);
    void Message(const char* text, float seconds);
    void FinishRun(bool completed);
    void Grade();
    // The rubric, live. Answers "what would I score if I got out right now", so
    // the number on the HUD is the same one the results screen will show.
    int LiveScore() const;

    Vec2 WellAccel(Vec2 p) const;
    void Blast(Vec2 at, float radius, float damage, Vec3 col);
    void DetonateMine(Mine& m);
    void KillWell(Well& w);
    void DestroyProp(Prop& p, bool violent);
    void HurtProp(Prop& p, float dmg, Vec2 at);
    int PropAt(Vec2 p, float radius) const;
    void LatchOrb();
    void ReleaseOrb();

    Vec2 WorldToUv(Vec2 world) const;
    bool LineOfSight(Vec2 a, Vec2 b) const;

    Renderer* r_ = nullptr;
    Synth* audio_ = nullptr;
    Window* win_ = nullptr;

    Mode mode_ = Mode::Title;
    float modeTime_ = 0;
    bool quit_ = false;
    bool paused_ = false;
    bool showHelp_ = false;

    LevelSpec spec_;
    Field field_;
    World world_;
    Profile profile_;

    GpuMesh terrainMesh_, caveMesh_, shipMesh_, podMesh_, fuelMesh_, turretBase_,
        turretBarrel_, reactorRing_, reactorCore_, bulletMesh_, mineMesh_, droneMesh_, droneEye_,
        wellRing_, wellCore_, tankMesh_, tankCore_, crateMesh_, boulderMesh_, slabMesh_,
        relicMesh_, pedestalMesh_;

    // ship - the body is authoritative, these are the cached copies everything
    // else in the game reads.
    int shipBody_ = -1;
    Vec2 shipPos_, shipVel_;
    float shipAng_ = 0, shipAngVel_ = 0;
    float fuel_ = 100, shield_ = 100, dilation_ = 100;
    float thrustVis_ = 0, retroVis_ = 0, gunCooldown_ = 0, shieldVis_ = 0;
    float shieldLock_ = 0, shieldFlash_ = 0, shieldProx_ = 0;
    bool thrusting_ = false, retro_ = false, overcharge_ = false, tractor_ = false;
    bool tractorPress_ = false;  // one release edge per frame, not per physics step
    bool slowMo_ = false;
    float invuln_ = 0;
    float timeScale_ = 1.0f;

    // orb on its rope
    int podBody_ = -1;
    std::vector<int> ropeLinks_;
    std::vector<int> ropeJoints_;
    Vec2 podPos_;
    bool podAttached_ = false, podDelivered_ = false;
    float podSpin_ = 0;
    float tetherTension_ = 0;

    // reactor
    Vec2 reactorPos_;
    int reactorHits_ = 0;
    bool reactorDead_ = false;
    float turretStun_ = 0;
    float meltdownVis_ = 0;  // short shockfront after the factory goes, not a clock

    std::vector<Prop> props_;
    std::vector<Relic> relics_;
    std::vector<Turret> turrets_;
    std::vector<Mine> mines_;
    std::vector<Drone> drones_;
    std::vector<Well> wells_;
    std::vector<FuelCell> fuel_cells_;
    std::vector<Bullet> bullets_;
    std::vector<Particle> parts_;
    std::vector<Shard> shards_;
    std::vector<Star> stars_;
    std::vector<Star> nebulae_;

    // planet select
    int gridSel_ = 0;
    std::vector<uint8_t> thumbs_;  // kPlanetCount * kThumbW * kThumbH
    std::vector<LevelSpec> specs_;
    RunStats run_;
    const char* failReason_ = "";

    // camera / post
    Vec2 camPos_ = {};
    float camZ_ = -80.0f, camZTarget_ = -80.0f;
    float shake_ = 0;
    float flash_ = 0;
    Vec3 flashCol_ = {1, 1, 1};
    Mat4 viewProj_;
    struct Shock {
        Vec2 world;
        float t = 0;
        float strength = 0;
    };
    Shock shocks_[4];

    float time_ = 0;
    float msgTime_ = 0;
    std::string msg_;
    std::string newThreat_;
    bool metMine_ = false, metDrone_ = false, metWell_ = false, metRepulsor_ = false;
    float wellBoost_ = 1.0f;
    Rng rng_{99991};
    // The shake draws once per rendered frame. On rng_ that would make turret
    // timing and fuses depend on the frame rate.
    Rng shakeRng_{4241};
    float accum_ = 0;
};

}  // namespace zp

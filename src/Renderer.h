// zpilot - portable scene-renderer contract.
//
// The implementation is an explicit Direct3D 11 backend. Keeping this
// contract free of the backend's headers lets gameplay, geometry and font code
// remain platform-neutral without weakening the game's renderer.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"
#include <libdesktop/platform.h>

namespace zp {

struct Vtx {
    Vec3 p;
    Vec3 n;
    Vec3 c;
    float glow = 0;
};

struct RendererMesh;

struct GpuMesh {
    std::shared_ptr<RendererMesh> backend;
    uint32_t count = 0;
    // Material vocabulary, fixed when the mesh is built: made things get a
    // highlight, natural things get shader-side rock grain, never both.
    float gloss = 0;
    float grain = 0;
    bool valid() const { return count > 0 && backend != nullptr; }
};

// A lamp in the world. There are no fixtures in the cavern, so every one of
// these is the ship, an objective or something that has just gone off.
struct Light {
    Vec3 pos;
    float radius = 0;
    Vec3 colour;  // HDR, intensity already folded in
};
constexpr int kMaxLights = 8;

struct PostFx {
    float time = 0;
    float aberration = 1.0f;
    float vignette = 1.0f;
    float grain = 1.0f;
    float bloomThreshold = 0.75f;
    float bloomIntensity = 1.0f;
    float exposure = 1.0f;
    Vec3 bloomWeights = {0.75f, 0.55f, 0.40f};
    Vec4 shock[4] = {};
    Vec4 flash = {};
};

class RendererD3D;

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init(const platform::window_ptr& frame, int w, int h);
    const std::string& Error() const { return error_; }
    void Shutdown();
    void Resize(int w, int h);

    int Width() const { return width_; }
    int Height() const { return height_; }
    float Aspect() const { return (float)width_ / (float)std::max(1, height_); }

    GpuMesh CreateMesh(const std::vector<Vtx>& verts, float gloss = 0.0f, float grain = 0.0f);

    // Lights are frame constants, so they must be submitted before BeginScene,
    // which consumes and clears the set. Beyond kMaxLights the brightest win.
    void AddLight(Vec3 pos, float radius, Vec3 colour);

    void BeginScene(const Mat4& viewProj, Vec3 camPos, Vec3 camRight, Vec3 camUp, float time,
                    Vec3 shipPos, float torchRadius, float darkness, float meltdown, float warp,
                    float fogDensity, Vec4 plasma, Vec3 clearColor);
    // warpResponse opts the mesh into the meltdown shear; props stay rigid.
    void DrawMesh(const GpuMesh& m, const Mat4& world, Vec4 tint, Vec4 emissive,
                  float warpResponse = 0.0f);

    void AddLine(Vec3 a, Vec3 b, float halfWidth, Vec4 color);
    // Soft edged alpha blended slab, drawn under the additive batches.
    void AddPanel(Vec2 topLeft, Vec2 size, Vec4 color);
    void AddGlow(Vec3 p, float size, Vec4 color, int type = 0, float rot = 0, float stretch = 0,
                 float phase = 0);
    void AddShard(Vec3 p, float size, Vec4 quat, Vec4 color);

    void FlushEffects();
    void BeginHud();  // flush world effects, switch to a pixel-space camera
    void EndScene(const PostFx& fx);
    void Present(bool vsync);
    bool SaveScreenshot(const std::filesystem::path& path);  // call before Present

private:
    std::unique_ptr<RendererD3D> backend_;
    std::string error_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace zp

#include "Renderer.h"

#include "RendererD3D.h"

namespace zp {

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

bool Renderer::Init(const platform::window_ptr& frame, int w, int h) {
    backend_ = std::make_unique<RendererD3D>();
    if (!backend_->Init(frame, w, h)) {
        error_ = backend_->Error();
        backend_.reset();
        return false;
    }
    error_.clear();
    width_ = w;
    height_ = h;
    return true;
}

void Renderer::Shutdown() {
    if (backend_) backend_->Shutdown();
}

void Renderer::Resize(int w, int h) {
    if (!backend_) return;
    backend_->Resize(w, h);
    width_ = backend_->Width();
    height_ = backend_->Height();
}

GpuMesh Renderer::CreateMesh(const std::vector<Vtx>& verts, float gloss, float grain) {
    return backend_ ? backend_->CreateMesh(verts, gloss, grain) : GpuMesh{};
}

void Renderer::AddLight(Vec3 pos, float radius, Vec3 colour) {
    if (backend_) backend_->AddLight(pos, radius, colour);
}

void Renderer::BeginScene(const Mat4& viewProj, Vec3 camPos, Vec3 camRight, Vec3 camUp, float time,
                          Vec3 shipPos, float torchRadius, float darkness, float meltdown, float warp,
                          float fogDensity, Vec4 plasma, Vec3 clearColor) {
    if (backend_) backend_->BeginScene(viewProj, camPos, camRight, camUp, time, shipPos, torchRadius,
                                       darkness, meltdown, warp, fogDensity, plasma, clearColor);
}

void Renderer::DrawMesh(const GpuMesh& m, const Mat4& world, Vec4 tint, Vec4 emissive,
                        float warpResponse) {
    if (backend_) backend_->DrawMesh(m, world, tint, emissive, warpResponse);
}

void Renderer::AddLine(Vec3 a, Vec3 b, float halfWidth, Vec4 color) {
    if (backend_) backend_->AddLine(a, b, halfWidth, color);
}

void Renderer::AddPanel(Vec2 topLeft, Vec2 size, Vec4 color) {
    if (backend_) backend_->AddPanel(topLeft, size, color);
}

void Renderer::AddGlow(Vec3 p, float size, Vec4 color, int type, float rot, float stretch, float phase) {
    if (backend_) backend_->AddGlow(p, size, color, type, rot, stretch, phase);
}

void Renderer::AddShard(Vec3 p, float size, Vec4 quat, Vec4 color) {
    if (backend_) backend_->AddShard(p, size, quat, color);
}

void Renderer::FlushEffects() {
    if (backend_) backend_->FlushEffects();
}

void Renderer::BeginHud() {
    if (backend_) backend_->BeginHud();
}

void Renderer::EndScene(const PostFx& fx) {
    if (backend_) backend_->EndScene(fx);
}

void Renderer::Present(bool vsync) {
    if (backend_) backend_->Present(vsync);
}

bool Renderer::SaveScreenshot(const std::filesystem::path& path) {
    return backend_ && backend_->SaveScreenshot(path);
}

}  // namespace zp

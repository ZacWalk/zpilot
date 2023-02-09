// z-pilot - Direct3D 11 renderer: HDR scene buffer, additive effect batches,
// three level bloom chain and a shockwave/CRT composite pass.
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "Common.h"

namespace zp {

using Microsoft::WRL::ComPtr;

struct Vtx {
    Vec3 p;
    Vec3 n;
    Vec3 c;
    float glow = 0;
};

struct GpuMesh {
    ComPtr<ID3D11Buffer> vb;
    UINT count = 0;
    // Material vocabulary, fixed when the mesh is built: made things get a
    // highlight, natural things get shader-side rock grain, never both.
    float gloss = 0;
    float grain = 0;
    bool valid() const { return count > 0; }
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

class Renderer {
public:
    bool Init(HWND hwnd, int w, int h);
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
    bool SaveScreenshot(const wchar_t* path);  // call before Present

private:
    struct RT {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
    };

    bool CreateShaders();
    bool CreateBuffers();
    void CreateSizedTargets();
    void ReleaseSizedTargets();
    bool MakeRT(RT& rt, int w, int h, DXGI_FORMAT fmt);
    void SetViewport(int w, int h);
    void FullscreenPass(ID3D11PixelShader* ps, const RT* dst, ID3D11ShaderResourceView* const* srvs,
                        UINT srvCount);
    void UpdateFrameCB();
    void FlushPanels();
    void FlushLines();
    void FlushGlow();
    void FlushShards();

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;

    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> backRtv_;
    ComPtr<ID3D11DepthStencilView> depthDsv_;
    ComPtr<ID3D11Texture2D> depthTex_;
    RT scene_;
    RT bloomA_[3];
    RT bloomB_[3];

    ComPtr<ID3D11VertexShader> vsSolid_, vsLine_, vsGlow_, vsShard_, vsPanel_, vsFull_;
    ComPtr<ID3D11PixelShader> psSolid_, psLine_, psGlow_, psShard_, psPanel_, psBright_, psBlur_,
        psCopy_, psComposite_;
    ComPtr<ID3D11InputLayout> ilSolid_, ilLine_, ilGlow_, ilShard_, ilPanel_;

    ComPtr<ID3D11Buffer> frameCB_, objectCB_, postCB_;
    ComPtr<ID3D11Buffer> lineVB_, glowIB_, shardIB_, shardVB_, panelVB_;
    UINT shardVertexCount_ = 0;

    ComPtr<ID3D11RasterizerState> rsSolid_, rsNoCull_;
    ComPtr<ID3D11DepthStencilState> dsWrite_, dsTest_, dsOff_;
    ComPtr<ID3D11BlendState> bsOpaque_, bsAdd_, bsAlpha_;
    ComPtr<ID3D11SamplerState> smpLinear_;

    struct LineVtx {
        Vec3 p;
        Vec3 uvh;
        Vec4 c;
    };
    struct PanelVtx {
        Vec3 p;
        Vec2 uv;
        Vec4 c;
    };
    struct GlowInst {
        Vec4 posSize;
        Vec4 col;
        Vec4 shape;
    };
    struct ShardInst {
        Vec4 posSize;
        Vec4 quat;
        Vec4 col;
    };

    std::vector<LineVtx> lines_;
    std::vector<PanelVtx> panels_;
    std::vector<GlowInst> glows_;
    std::vector<ShardInst> shards_;

    struct FrameCBData {
        Mat4 viewProj;
        Vec4 camPos;
        Vec4 camRight;
        Vec4 camUp;
        Vec4 ship;
        Vec4 params;
        Vec4 plasma;
        Vec4 sky;
        Vec4 lightPos[kMaxLights];
        Vec4 lightCol[kMaxLights];
    } frame_ = {};

    Light lights_[kMaxLights] = {};
    float lightScore_[kMaxLights] = {};

    bool hudMode_ = false;
};

}  // namespace zp

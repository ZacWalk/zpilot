#include "RendererD3D.h"

#include <d3dcompiler.h>
#include <new>

#include <libfiles/encode.h>

#include "Shaders.h"
#include "platform_win_interop.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace zp {

static const UINT kMaxLineVerts = 150000;
static const UINT kMaxPanelVerts = 3000;
static const UINT kMaxGlow = 60000;
static const UINT kMaxShards = 8000;

static ComPtr<ID3DBlob> CompileShader(const char* prefix, const char* src, const char* target,
                                      std::string* error) {
    std::string full;
    if (prefix) full += prefix;
    full += src;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompile(full.c_str(), full.size(), nullptr, nullptr, nullptr, "main", target,
                            flags, 0, &code, &err);
    if (FAILED(hr)) {
        std::string msg = "Shader compile failed (";
        msg += target;
        msg += "):\n";
        if (err) msg += (const char*)err->GetBufferPointer();
        if (error) *error = msg;
        pf::debug_trace(msg);
        return nullptr;
    }
    return code;
}

bool RendererD3D::Init(const pf::window_frame_ptr& frame, int w, int h) {
    error_.clear();
    const uintptr_t nativeWindow = pf::platform_win::native_window_handle(frame);
    if (!nativeWindow) {
        error_ = "Direct3D 11 renderer: no native window target";
        return false;
    }
    nativeWindow_ = nativeWindow;
    width_ = w;
    height_ = h;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   _countof(levels), D3D11_SDK_VERSION, &dev_, &got, &ctx_);
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               _countof(levels), D3D11_SDK_VERSION, &dev_, &got, &ctx_);
    }
    if (FAILED(hr)) {
        error_ = "Direct3D 11 renderer: device creation failed";
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDev;
    if (FAILED(dev_.As(&dxgiDev))) {
        error_ = "Direct3D 11 renderer: DXGI device unavailable";
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDev->GetAdapter(&adapter))) {
        error_ = "Direct3D 11 renderer: adapter unavailable";
        return false;
    }
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)factory.GetAddressOf())))
    {
        error_ = "Direct3D 11 renderer: DXGI factory unavailable";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_STRETCH;
    if (FAILED(factory->CreateSwapChainForHwnd(
            dev_.Get(), reinterpret_cast<HWND>(nativeWindow), &sd, nullptr, nullptr, &swap_)))
    {
        error_ = "Direct3D 11 renderer: swap-chain creation failed";
        return false;
    }
    factory->MakeWindowAssociation(reinterpret_cast<HWND>(nativeWindow), DXGI_MWA_NO_ALT_ENTER);

    if (!CreateShaders()) return false;
    if (!CreateBuffers()) return false;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsSolid_);
    rd.CullMode = D3D11_CULL_NONE;
    dev_->CreateRasterizerState(&rd, &rsNoCull_);

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    dev_->CreateDepthStencilState(&dd, &dsWrite_);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev_->CreateDepthStencilState(&dd, &dsTest_);
    dd.DepthEnable = FALSE;
    dev_->CreateDepthStencilState(&dd, &dsOff_);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &bsOpaque_);
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dev_->CreateBlendState(&bd, &bsAdd_);
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    dev_->CreateBlendState(&bd, &bsAlpha_);

    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    dev_->CreateSamplerState(&sm, &smpLinear_);

    CreateSizedTargets();

    lines_.reserve(kMaxLineVerts);
    panels_.reserve(kMaxPanelVerts);
    glows_.reserve(kMaxGlow);
    shards_.reserve(kMaxShards);
    return true;
}

void RendererD3D::Shutdown() {
    if (ctx_) ctx_->ClearState();
}

bool RendererD3D::CreateShaders() {
    const char* c = shaders::kCommon;

    auto vsBlob = CompileShader(c, shaders::kSolidVS, "vs_5_0", &error_);
    auto psBlob = CompileShader(c, shaders::kSolidPS, "ps_5_0", &error_);
    if (!vsBlob || !psBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsSolid_);
    dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psSolid_);
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        dev_->CreateInputLayout(el, _countof(el), vsBlob->GetBufferPointer(),
                                vsBlob->GetBufferSize(), &ilSolid_);
    }

    vsBlob = CompileShader(c, shaders::kLineVS, "vs_5_0", &error_);
    psBlob = CompileShader(c, shaders::kLinePS, "ps_5_0", &error_);
    if (!vsBlob || !psBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsLine_);
    dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psLine_);
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        dev_->CreateInputLayout(el, _countof(el), vsBlob->GetBufferPointer(),
                                vsBlob->GetBufferSize(), &ilLine_);
    }

    vsBlob = CompileShader(c, shaders::kGlowVS, "vs_5_0", &error_);
    psBlob = CompileShader(c, shaders::kGlowPS, "ps_5_0", &error_);
    if (!vsBlob || !psBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsGlow_);
    dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psGlow_);
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        dev_->CreateInputLayout(el, _countof(el), vsBlob->GetBufferPointer(),
                                vsBlob->GetBufferSize(), &ilGlow_);
    }

    vsBlob = CompileShader(c, shaders::kShardVS, "vs_5_0", &error_);
    psBlob = CompileShader(c, shaders::kShardPS, "ps_5_0", &error_);
    if (!vsBlob || !psBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                             &vsShard_);
    dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psShard_);
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        dev_->CreateInputLayout(el, _countof(el), vsBlob->GetBufferPointer(),
                                vsBlob->GetBufferSize(), &ilShard_);
    }

    vsBlob = CompileShader(c, shaders::kPanelVS, "vs_5_0", &error_);
    psBlob = CompileShader(c, shaders::kPanelPS, "ps_5_0", &error_);
    if (!vsBlob || !psBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                             &vsPanel_);
    dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psPanel_);
    {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        dev_->CreateInputLayout(el, _countof(el), vsBlob->GetBufferPointer(),
                                vsBlob->GetBufferSize(), &ilPanel_);
    }

    const char* pc = shaders::kPostCommon;
    vsBlob = CompileShader(nullptr, shaders::kFullscreenVS, "vs_5_0", &error_);
    if (!vsBlob) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsFull_);

    struct {
        const char* src;
        ComPtr<ID3D11PixelShader>* out;
    } list[] = {{shaders::kBrightPS, &psBright_},
                {shaders::kBlurPS, &psBlur_},
                {shaders::kCopyPS, &psCopy_},
                {shaders::kCompositePS, &psComposite_}};
    for (auto& e : list) {
        auto b = CompileShader(pc, e.src, "ps_5_0", &error_);
        if (!b) return false;
        dev_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr,
                                e.out->GetAddressOf());
    }
    return true;
}

bool RendererD3D::CreateBuffers() {
    auto makeCB = [&](UINT size, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (size + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &out));
    };
    if (!makeCB(sizeof(FrameCBData), frameCB_)) return false;
    if (!makeCB(112, objectCB_)) return false;
    if (!makeCB(144, postCB_)) return false;

    auto makeDyn = [&](UINT bytes, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = bytes;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &out));
    };
    if (!makeDyn(kMaxLineVerts * sizeof(LineVtx), lineVB_)) return false;
    if (!makeDyn(kMaxPanelVerts * sizeof(PanelVtx), panelVB_)) return false;
    if (!makeDyn(kMaxGlow * sizeof(GlowInst), glowIB_)) return false;
    if (!makeDyn(kMaxShards * sizeof(ShardInst), shardIB_)) return false;

    // Debris shard: an irregular tetrahedron with flat normals.
    struct SV {
        Vec3 p, n;
    };
    Vec3 t[4] = {{0.0f, 1.0f, 0.0f}, {-0.85f, -0.45f, -0.5f}, {0.9f, -0.4f, -0.45f},
                 {0.0f, -0.35f, 0.95f}};
    int idx[12] = {0, 2, 1, 0, 3, 2, 0, 1, 3, 1, 2, 3};
    std::vector<SV> sv;
    for (int i = 0; i < 12; i += 3) {
        Vec3 a = t[idx[i]], b = t[idx[i + 1]], cc = t[idx[i + 2]];
        Vec3 n = normalize(cross(b - a, cc - a));
        sv.push_back({a, n});
        sv.push_back({b, n});
        sv.push_back({cc, n});
    }
    shardVertexCount_ = (UINT)sv.size();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sv.size() * sizeof(SV));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr = {sv.data(), 0, 0};
    return SUCCEEDED(dev_->CreateBuffer(&bd, &sr, &shardVB_));
}

bool RendererD3D::MakeRT(RT& rt, int w, int h, DXGI_FORMAT fmt) {
    rt = RT{};
    rt.w = std::max(1, w);
    rt.h = std::max(1, h);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = rt.w;
    td.Height = rt.h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &rt.tex))) return false;
    if (FAILED(dev_->CreateRenderTargetView(rt.tex.Get(), nullptr, &rt.rtv))) return false;
    return SUCCEEDED(dev_->CreateShaderResourceView(rt.tex.Get(), nullptr, &rt.srv));
}

void RendererD3D::ReleaseSizedTargets() {
    backRtv_.Reset();
    depthDsv_.Reset();
    depthTex_.Reset();
    scene_ = RT{};
    for (int i = 0; i < 3; ++i) {
        bloomA_[i] = RT{};
        bloomB_[i] = RT{};
    }
}

void RendererD3D::CreateSizedTargets() {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()))) return;
    dev_->CreateRenderTargetView(back.Get(), nullptr, &backRtv_);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dev_->CreateTexture2D(&td, nullptr, &depthTex_);
    dev_->CreateDepthStencilView(depthTex_.Get(), nullptr, &depthDsv_);

    MakeRT(scene_, width_, height_, DXGI_FORMAT_R16G16B16A16_FLOAT);
    int div = 2;
    for (int i = 0; i < 3; ++i) {
        MakeRT(bloomA_[i], width_ / div, height_ / div, DXGI_FORMAT_R16G16B16A16_FLOAT);
        MakeRT(bloomB_[i], width_ / div, height_ / div, DXGI_FORMAT_R16G16B16A16_FLOAT);
        div *= 2;
    }
}

void RendererD3D::Resize(int w, int h) {
    if (!swap_ || w <= 0 || h <= 0) return;
    if (w == width_ && h == height_) return;
    width_ = w;
    height_ = h;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseSizedTargets();
    swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateSizedTargets();
}

GpuMesh RendererD3D::CreateMesh(const std::vector<Vtx>& verts, float gloss, float grain) {
    GpuMesh m;
    m.gloss = gloss;
    m.grain = grain;
    if (verts.empty()) return m;
    auto mesh = std::make_shared<RendererMesh>();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(verts.size() * sizeof(Vtx));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr = {verts.data(), 0, 0};
    if (SUCCEEDED(dev_->CreateBuffer(&bd, &sr, &mesh->vb))) {
        m.backend = std::move(mesh);
        m.count = (uint32_t)verts.size();
    }
    return m;
}

void RendererD3D::SetViewport(int w, int h) {
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &vp);
}

void RendererD3D::UpdateFrameCB() {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx_->Map(frameCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, &frame_, sizeof(frame_));
        ctx_->Unmap(frameCB_.Get(), 0);
    }
    ID3D11Buffer* cbs[] = {frameCB_.Get()};
    ctx_->VSSetConstantBuffers(0, 1, cbs);
    ctx_->PSSetConstantBuffers(0, 1, cbs);
}

void RendererD3D::AddLight(Vec3 pos, float radius, Vec3 colour) {
    // Reach times brightness: a big soft blast wave should beat a pinpoint
    // relic even though the relic is the hotter colour.
    float score = (colour.x + colour.y + colour.z) * radius;
    if (score <= 0.0f) return;
    int worst = 0;
    for (int i = 1; i < kMaxLights; ++i)
        if (lightScore_[i] < lightScore_[worst]) worst = i;
    if (score <= lightScore_[worst]) return;
    lightScore_[worst] = score;
    lights_[worst] = {pos, radius, colour};
}

void RendererD3D::BeginScene(const Mat4& viewProj, Vec3 camPos, Vec3 camRight, Vec3 camUp, float time,
                          Vec3 shipPos, float torchRadius, float darkness, float meltdown,
                          float warp, float fogDensity, Vec4 plasma, Vec3 clearColor) {
    hudMode_ = false;
    lines_.clear();
    panels_.clear();
    glows_.clear();
    shards_.clear();

    ID3D11ShaderResourceView* nulls[4] = {};
    ctx_->PSSetShaderResources(0, 4, nulls);

    ID3D11RenderTargetView* rtvs[] = {scene_.rtv.Get()};
    ctx_->OMSetRenderTargets(1, rtvs, depthDsv_.Get());
    SetViewport(width_, height_);
    float clear[4] = {clearColor.x, clearColor.y, clearColor.z, 1.0f};
    ctx_->ClearRenderTargetView(scene_.rtv.Get(), clear);
    ctx_->ClearDepthStencilView(depthDsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    frame_.viewProj = viewProj;
    frame_.camPos = Vec4(camPos, time);
    frame_.camRight = Vec4(camRight, 0);
    frame_.camUp = Vec4(camUp, 0);
    frame_.ship = Vec4(shipPos, torchRadius);
    frame_.params = Vec4(darkness, meltdown, warp, fogDensity);
    frame_.plasma = plasma;
    frame_.sky = Vec4(clearColor, 0);
    for (int i = 0; i < kMaxLights; ++i) {
        frame_.lightPos[i] = Vec4(lights_[i].pos, lights_[i].radius);
        frame_.lightCol[i] = Vec4(lights_[i].colour, 0);
        lights_[i] = Light{};
        lightScore_[i] = 0;
    }
    UpdateFrameCB();

    ctx_->RSSetState(rsSolid_.Get());
    ctx_->OMSetDepthStencilState(dsWrite_.Get(), 0);
    float bf[4] = {1, 1, 1, 1};
    ctx_->OMSetBlendState(bsOpaque_.Get(), bf, 0xffffffff);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void RendererD3D::DrawMesh(const GpuMesh& m, const Mat4& world, Vec4 tint, Vec4 emissive,
                        float warpResponse) {
    if (!m.valid()) return;

    struct {
        Mat4 world;
        Vec4 tint;
        Vec4 emissive;
        Vec4 object;
    } obj = {world, tint, emissive, Vec4(warpResponse, m.gloss, m.grain, 0)};

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx_->Map(objectCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, &obj, sizeof(obj));
        ctx_->Unmap(objectCB_.Get(), 0);
    }
    ID3D11Buffer* cbs[] = {objectCB_.Get()};
    ctx_->VSSetConstantBuffers(1, 1, cbs);
    ctx_->PSSetConstantBuffers(1, 1, cbs);

    ctx_->IASetInputLayout(ilSolid_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vtx), offset = 0;
    ID3D11Buffer* vbs[] = {m.backend->vb.Get()};
    ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx_->VSSetShader(vsSolid_.Get(), nullptr, 0);
    ctx_->PSSetShader(psSolid_.Get(), nullptr, 0);
    ctx_->Draw(m.count, 0);
}

void RendererD3D::AddLine(Vec3 a, Vec3 b, float halfWidth, Vec4 color) {
    if (lines_.size() + 6 > kMaxLineVerts) return;
    Vec2 d = b.xy() - a.xy();
    float len = length(d);
    Vec2 dir = len > 1e-5f ? d / len : Vec2(1, 0);
    Vec2 pp = perp(dir) * halfWidth;
    Vec2 ext = dir * halfWidth;
    float hl = (len * 0.5f + halfWidth) / std::max(halfWidth, 1e-5f);

    Vec3 A = Vec3(a.x - ext.x, a.y - ext.y, a.z);
    Vec3 B = Vec3(b.x + ext.x, b.y + ext.y, b.z);
    LineVtx v0{{A.x + pp.x, A.y + pp.y, A.z}, {1, -1, hl}, color};
    LineVtx v1{{B.x + pp.x, B.y + pp.y, B.z}, {1, 1, hl}, color};
    LineVtx v2{{B.x - pp.x, B.y - pp.y, B.z}, {-1, 1, hl}, color};
    LineVtx v3{{A.x - pp.x, A.y - pp.y, A.z}, {-1, -1, hl}, color};
    lines_.push_back(v0);
    lines_.push_back(v1);
    lines_.push_back(v2);
    lines_.push_back(v0);
    lines_.push_back(v2);
    lines_.push_back(v3);
}

void RendererD3D::AddPanel(Vec2 topLeft, Vec2 size, Vec4 color) {
    if (panels_.size() + 6 > kMaxPanelVerts) return;
    const float x0 = topLeft.x, y0 = topLeft.y;
    const float x1 = x0 + size.x, y1 = y0 + size.y;
    PanelVtx v0{{x0, y0, 0}, {-1, -1}, color};
    PanelVtx v1{{x1, y0, 0}, {1, -1}, color};
    PanelVtx v2{{x1, y1, 0}, {1, 1}, color};
    PanelVtx v3{{x0, y1, 0}, {-1, 1}, color};
    panels_.push_back(v0);
    panels_.push_back(v1);
    panels_.push_back(v2);
    panels_.push_back(v0);
    panels_.push_back(v2);
    panels_.push_back(v3);
}

void RendererD3D::AddGlow(Vec3 p, float size, Vec4 color, int type, float rot, float stretch,
                       float phase) {
    if (glows_.size() >= kMaxGlow) return;
    glows_.push_back({Vec4(p, size), color, Vec4(rot, (float)type, stretch, phase)});
}

void RendererD3D::AddShard(Vec3 p, float size, Vec4 quat, Vec4 color) {
    if (shards_.size() >= kMaxShards) return;
    shards_.push_back({Vec4(p, size), quat, color});
}

void RendererD3D::FlushPanels() {
    if (panels_.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx_->Map(panelVB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, panels_.data(), panels_.size() * sizeof(PanelVtx));
    ctx_->Unmap(panelVB_.Get(), 0);

    float bf[4] = {1, 1, 1, 1};
    ctx_->OMSetBlendState(bsAlpha_.Get(), bf, 0xffffffff);
    ctx_->IASetInputLayout(ilPanel_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(PanelVtx), offset = 0;
    ID3D11Buffer* vbs[] = {panelVB_.Get()};
    ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx_->VSSetShader(vsPanel_.Get(), nullptr, 0);
    ctx_->PSSetShader(psPanel_.Get(), nullptr, 0);
    ctx_->Draw((UINT)panels_.size(), 0);
    panels_.clear();
    ctx_->OMSetBlendState(bsAdd_.Get(), bf, 0xffffffff);
}

void RendererD3D::FlushLines() {
    if (lines_.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx_->Map(lineVB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, lines_.data(), lines_.size() * sizeof(LineVtx));
    ctx_->Unmap(lineVB_.Get(), 0);

    ctx_->IASetInputLayout(ilLine_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(LineVtx), offset = 0;
    ID3D11Buffer* vbs[] = {lineVB_.Get()};
    ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx_->VSSetShader(vsLine_.Get(), nullptr, 0);
    ctx_->PSSetShader(psLine_.Get(), nullptr, 0);
    ctx_->Draw((UINT)lines_.size(), 0);
    lines_.clear();
}

void RendererD3D::FlushGlow() {
    if (glows_.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx_->Map(glowIB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, glows_.data(), glows_.size() * sizeof(GlowInst));
    ctx_->Unmap(glowIB_.Get(), 0);

    ctx_->IASetInputLayout(ilGlow_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT stride = sizeof(GlowInst), offset = 0;
    ID3D11Buffer* vbs[] = {glowIB_.Get()};
    ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx_->VSSetShader(vsGlow_.Get(), nullptr, 0);
    ctx_->PSSetShader(psGlow_.Get(), nullptr, 0);
    ctx_->DrawInstanced(4, (UINT)glows_.size(), 0, 0);
    glows_.clear();
}

void RendererD3D::FlushShards() {
    if (shards_.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx_->Map(shardIB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, shards_.data(), shards_.size() * sizeof(ShardInst));
    ctx_->Unmap(shardIB_.Get(), 0);

    ctx_->IASetInputLayout(ilShard_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT strides[2] = {sizeof(Vec3) * 2, sizeof(ShardInst)};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer* vbs[2] = {shardVB_.Get(), shardIB_.Get()};
    ctx_->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    ctx_->VSSetShader(vsShard_.Get(), nullptr, 0);
    ctx_->PSSetShader(psShard_.Get(), nullptr, 0);
    ctx_->DrawInstanced(shardVertexCount_, (UINT)shards_.size(), 0, 0);
    shards_.clear();
}

void RendererD3D::FlushEffects() {
    if (lines_.empty() && glows_.empty() && shards_.empty() && panels_.empty()) return;
    float bf[4] = {1, 1, 1, 1};
    ctx_->OMSetBlendState(bsAdd_.Get(), bf, 0xffffffff);
    ctx_->OMSetDepthStencilState(hudMode_ ? dsOff_.Get() : dsTest_.Get(), 0);
    ctx_->RSSetState(rsNoCull_.Get());
    FlushPanels();
    FlushShards();
    FlushLines();
    FlushGlow();
}

void RendererD3D::BeginHud() {
    FlushEffects();
    hudMode_ = true;
    frame_.viewProj = Mat4::screenOrtho((float)width_, (float)height_);
    ctx_->OMSetDepthStencilState(dsOff_.Get(), 0);
}

void RendererD3D::FullscreenPass(ID3D11PixelShader* ps, const RT* dst,
                              ID3D11ShaderResourceView* const* srvs, UINT srvCount) {
    ID3D11ShaderResourceView* nulls[4] = {};
    ctx_->PSSetShaderResources(0, 4, nulls);

    ID3D11RenderTargetView* rtv = dst ? dst->rtv.Get() : backRtv_.Get();
    ID3D11RenderTargetView* rtvs[] = {rtv};
    ctx_->OMSetRenderTargets(1, rtvs, nullptr);
    SetViewport(dst ? dst->w : width_, dst ? dst->h : height_);

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* none[] = {nullptr};
    UINT z = 0;
    ctx_->IASetVertexBuffers(0, 1, none, &z, &z);
    ctx_->VSSetShader(vsFull_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps, nullptr, 0);
    ctx_->PSSetShaderResources(0, srvCount, srvs);
    ID3D11SamplerState* smps[] = {smpLinear_.Get()};
    ctx_->PSSetSamplers(0, 1, smps);
    ctx_->Draw(3, 0);
}

void RendererD3D::EndScene(const PostFx& fx) {
    // The HUD batch is deliberately left pending: it is drawn after the post
    // chain so text stays pixel crisp instead of being smeared by aberration.
    const bool hudPending = hudMode_;
    if (!hudPending) FlushEffects();

    struct PostCBData {
        Vec4 texel;
        Vec4 blur;
        Vec4 shock[4];
        Vec4 postA;
        Vec4 flash;
        Vec4 mix;
    } pc = {};
    for (int i = 0; i < 4; ++i) pc.shock[i] = fx.shock[i];
    pc.postA = Vec4(fx.time, fx.aberration, fx.vignette, fx.grain);
    pc.flash = fx.flash;
    pc.mix = Vec4(fx.bloomWeights.x, fx.bloomWeights.y, fx.bloomWeights.z, fx.exposure);

    auto upload = [&](const RT* target, Vec4 blur) {
        pc.texel = target ? Vec4(1.0f / target->w, 1.0f / target->h, (float)target->w,
                                 (float)target->h)
                          : Vec4(1.0f / width_, 1.0f / height_, (float)width_, (float)height_);
        pc.blur = blur;
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx_->Map(postCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            memcpy(ms.pData, &pc, sizeof(pc));
            ctx_->Unmap(postCB_.Get(), 0);
        }
        ID3D11Buffer* cbs[] = {postCB_.Get()};
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        ctx_->VSSetConstantBuffers(0, 1, cbs);
    };

    float bf[4] = {1, 1, 1, 1};
    ctx_->OMSetBlendState(bsOpaque_.Get(), bf, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_.Get(), 0);
    ctx_->RSSetState(rsNoCull_.Get());

    ID3D11ShaderResourceView* srv1[1];

    upload(&bloomA_[0], Vec4(0, 0, fx.bloomThreshold, fx.bloomIntensity));
    srv1[0] = scene_.srv.Get();
    FullscreenPass(psBright_.Get(), &bloomA_[0], srv1, 1);

    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            upload(&bloomA_[i], Vec4(0, 0, 0, 0));
            srv1[0] = bloomA_[i - 1].srv.Get();
            FullscreenPass(psCopy_.Get(), &bloomA_[i], srv1, 1);
        }
        upload(&bloomB_[i], Vec4(1, 0, 0, 0));
        srv1[0] = bloomA_[i].srv.Get();
        FullscreenPass(psBlur_.Get(), &bloomB_[i], srv1, 1);

        upload(&bloomA_[i], Vec4(0, 1, 0, 0));
        srv1[0] = bloomB_[i].srv.Get();
        FullscreenPass(psBlur_.Get(), &bloomA_[i], srv1, 1);
    }

    upload(nullptr, Vec4(0, 0, 0, 0));
    ID3D11ShaderResourceView* srv4[4] = {scene_.srv.Get(), bloomA_[0].srv.Get(),
                                         bloomA_[1].srv.Get(), bloomA_[2].srv.Get()};
    FullscreenPass(psComposite_.Get(), nullptr, srv4, 4);

    ID3D11ShaderResourceView* nulls[4] = {};
    ctx_->PSSetShaderResources(0, 4, nulls);

    if (hudPending) {
        ID3D11RenderTargetView* rtvs[] = {backRtv_.Get()};
        ctx_->OMSetRenderTargets(1, rtvs, nullptr);
        SetViewport(width_, height_);
        frame_.camUp.w = 1.0f;
        UpdateFrameCB();
        FlushEffects();
    }
}

void RendererD3D::Present(bool vsync) {
    if (swap_) swap_->Present(vsync ? 1 : 0, 0);
}

namespace {

pf::file_path SharedPath(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return pf::file_path(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}

bool PublishPng(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    const pf::file_path target = SharedPath(path);
    const pf::file_path folder = target.folder();
    if (!folder.empty() && !pf::platform_create_directory(folder)) {
        pf::debug_trace("zpilot: screenshot directory creation failed: " +
                        pf::platform_last_error_message());
        return false;
    }

    const auto opened = pf::create_atomic_file_writer_result(target);
    if (!opened.success()) {
        pf::debug_trace("zpilot: screenshot staging failed: " + opened.result.message);
        return false;
    }
    const auto writer = opened.writer;
    const auto written = writer->write(std::span<const uint8_t>(bytes.data(), bytes.size()));
    if (!written) {
        pf::debug_trace("zpilot: screenshot staging write failed: " + written.message);
        return false;
    }
    const auto flushed = writer->flush();
    if (!flushed) {
        pf::debug_trace("zpilot: screenshot flush failed: " + flushed.message);
        return false;
    }
    const auto committed = writer->commit();
    if (!committed) {
        pf::debug_trace("zpilot: screenshot publication failed: " + committed.message);
        return false;
    }
    const auto published = pf::query_file(target);
    if (!published.found()) {
        if (published.status == pf::file_query_status::failed)
            pf::debug_trace("zpilot: screenshot publication query failed: " +
                            published.error.message);
        else
            pf::debug_trace("zpilot: screenshot publication reported success but file is missing");
        return false;
    }
    return true;
}

struct MappedTexture {
    ID3D11DeviceContext* context = nullptr;
    ID3D11Resource* resource = nullptr;

    ~MappedTexture() {
        if (context && resource) context->Unmap(resource, 0);
    }
};

}  // namespace

bool RendererD3D::SaveScreenshot(const std::filesystem::path& path) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf())))
        return false;
    D3D11_TEXTURE2D_DESC td = {};
    back->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &staging))) return false;
    ctx_->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;

    std::vector<uint8_t> png;
    {
        MappedTexture mapped{ctx_.Get(), staging.Get()};
        try {
            const libfiles::image_view image = {
                static_cast<const uint8_t*>(ms.pData),
                ms.RowPitch,
                td.Width,
                td.Height,
                libfiles::pixel_format::bgra8,
            };
            auto encoded = libfiles::encode_png(image, 1, {});
            for (const auto& note : encoded.notes)
                pf::debug_trace("zpilot: PNG encoder: " + note);
            if (encoded.data.empty()) {
                pf::debug_trace("zpilot: PNG encode returned no data");
                return false;
            }
            png = std::move(encoded.data);
        } catch (const libfiles::encode_error& error) {
            pf::debug_trace(std::string("zpilot: PNG encode failed: ") + error.what());
            return false;
        } catch (const std::bad_alloc&) {
            pf::debug_trace("zpilot: PNG encode ran out of memory");
            return false;
        }
    }
    return PublishPng(path, png);
}

}  // namespace zp

// zpilot - all HLSL, embedded and compiled at runtime.
#pragma once

namespace zp {
namespace shaders {

// Shared constant buffer declarations for the scene pipelines.
static const char* kCommon = R"(
#define ZP_LIGHTS 8

cbuffer FrameCB : register(b0)
{
    row_major float4x4 gViewProj;
    float4 gCamPos;     // xyz = eye, w = time
    float4 gCamRight;   // xyz = camera right
    float4 gCamUp;      // xyz = camera up, w = 1 when drawing HUD to the LDR backbuffer
    float4 gShip;       // xyz = ship pos, w = torch radius
    float4 gParams;     // x = darkness, y = meltdown, z = warp, w = fogDensity
    float4 gPlasma;     // x = amount, y = world scale, z = speed, w = palette shift
    float4 gSky;        // rgb = the void this planet sits in, which the haze fades to
    float4 gLightPos[ZP_LIGHTS];   // xyz = world position, w = radius
    float4 gLightCol[ZP_LIGHTS];   // rgb = HDR colour, black when the slot is idle
};

cbuffer ObjectCB : register(b1)
{
    row_major float4x4 gWorld;
    float4 gTint;       // rgb multiplier, a = alpha
    float4 gEmissive;   // rgb emissive, a = detail strength
    float4 gObject;     // x = meltdown warp response, y = gloss, z = rock grain
};

float3 acesTonemap(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Distance based visibility used by the "blackout" levels, where the world is
// only revealed by the ship's own light.
float torchFactor(float3 wp)
{
    float d = length(wp.xy - gShip.xy);
    float t = saturate(1.0 - d / max(gShip.w, 1.0));
    t = t * t * (3.0 - 2.0 * t);
    return lerp(1.0, t, gParams.x);
}

// The scene pipelines render HDR, but the HUD is composited after tonemapping,
// so it needs its own roll off or every stroke clips to white.
float3 emitColour(float3 c)
{
    return gCamUp.w > 0.5 ? c / (1.0 + c) : c;
}

// Dave Hoskins' "Hash without Sine" (MIT License, Copyright (c) 2014 David
// Hoskins): no sin(), so it hashes the same on every GPU.
float vhash(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

float vnoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(vhash(i), vhash(i + float2(1, 0)), f.x),
                lerp(vhash(i + float2(0, 1)), vhash(i + float2(1, 1)), f.x), f.y);
}

// The planet slice is one enormous sheet at grid resolution, so all of its
// surface relief has to be found here or the rock reads as flat facets. Two
// octaves, the second folded, which fractures the grain instead of dimpling it.
float rockGrain(float2 p)
{
    float a = vnoise(p);
    float b = abs(vnoise(p * 2.31) - 0.5) * 2.0;
    return a * 0.62 + b * 0.38;
}

// Nothing in the cavern is lit from a fixture: every lamp in the frame is the
// ship, an objective or something the player has just set off. Diffuse only,
// and wrapped, because the bore is a single smooth surface and a hard
// terminator across it reads as a shading fault rather than as a lamp.
float3 dynamicLights(float3 wp, float3 n)
{
    float3 sum = 0;
    [loop] for (int i = 0; i < ZP_LIGHTS; ++i)
    {
        float3 c = gLightCol[i].rgb;
        if (dot(c, 1.0.xxx) < 1e-4) continue;
        float3 d = gLightPos[i].xyz - wp;
        float dist2 = dot(d, d);
        float r = max(gLightPos[i].w, 1e-3);
        float fall = saturate(1.0 - dist2 / (r * r));
        fall *= fall;
        float3 L = d * rsqrt(max(dist2, 1e-4));
        sum += c * fall * saturate(dot(n, L) * 0.62 + 0.38);
    }
    return sum;
}

// The 1990 demo plasma: four sine terms summed into one index, then read
// through a cyclic palette. Two are axis aligned, one diagonal, one radiates
// from a slowly orbiting focus - that last term is what makes it churn instead
// of scrolling. Evaluated in world space so it lives in the rock.
float3 plasmaField(float2 world)
{
    float t = gCamPos.w * gPlasma.z;
    float2 q = world / max(gPlasma.y, 0.001);
    float2 orbit = float2(sin(t * 0.41), cos(t * 0.33)) * 3.4;

    float index = sin(q.x * 1.30 + t);
    index += sin(q.y * 1.10 - t * 0.83);
    index += sin((q.x + q.y) * 0.72 + t * 1.27);
    index += sin(length(q - orbit) * 1.70 - t * 1.60);
    index *= 0.25;

    // Three phase shifted cosines wrap seamlessly, so the palette never seams.
    float i01 = index * 0.5 + 0.5 + gPlasma.w;
    return 0.5 + 0.5 * cos(6.2831853 * (i01 + float3(0.0, 0.34, 0.67)));
}
)";

// ---------------------------------------------------------------- solid mesh
static const char* kSolidVS = R"(
struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float3 col : COLOR;
    float  glow: TEXCOORD0;
};
struct VSOut {
    float4 svp : SV_POSITION;
    float3 wp  : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float3 col : TEXCOORD2;
    float  glow: TEXCOORD3;
};

VSOut main(VSIn i)
{
    VSOut o;
    float3 p = i.pos;

    // Meltdown shockwave: the planet breathes. The offset is a pure function of
    // world position, never of the normal, so faces that share an edge stay
    // welded instead of splitting the mesh into flying panels. Opted in per
    // object - props and the ship ride it out rigidly. Kept under a ship radius
    // so the rock never visibly swallows a hull the collider says is clear.
    float w = gParams.z * gObject.x;
    if (w > 0.001)
    {
        float3 wpre = mul(gWorld, float4(p, 1)).xyz;
        float2 shear = float2(sin(wpre.y * 0.085 + gCamPos.w * 4.1),
                              cos(wpre.x * 0.071 - gCamPos.w * 3.3));
        p.xy += shear * w * 1.4;
    }

    float4 wp = mul(gWorld, float4(p, 1));
    o.wp = wp.xyz;
    o.svp = mul(gViewProj, wp);
    o.nrm = mul((float3x3)gWorld, i.nrm);
    o.col = i.col;
    o.glow = i.glow;
    return o;
}
)";

static const char* kSolidPS = R"(
struct VSOut {
    float4 svp : SV_POSITION;
    float3 wp  : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float3 col : TEXCOORD2;
    float  glow: TEXCOORD3;
};

// Natural materials are drawn the way a 1990s software renderer drew rock: a
// point-sampled texture laid over the world and lit into a short ramp. Crisp
// texels and banded light, rather than smeared noise and smooth gradients.
static const float kTexel = 0.25;     // world units per rock texel
static const float kRampSteps = 2.5;  // light bands per doubling of brightness
static const float kDitherSpan = 0.5; // share of each band given to the dithered edge
static const float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

struct Texel
{
    float3 centre;  // the surface point every pixel of the texel is shaded at
    float2 uv;      // texel centre on the projection plane
    float2 cell;
    float3 ax, ay;  // world axes of the projection plane
};

// Projected onto whichever axis plane faces the surface best, so the cut face,
// the steep walls of a bore and the planet's skirt all get square texels
// instead of streaks.
Texel rockTexel(float3 wp, float3 n)
{
    Texel t;
    float3 a = abs(n);
    if (a.z >= max(a.x, a.y)) { t.ax = float3(1, 0, 0); t.ay = float3(0, 1, 0); }
    else if (a.x >= a.y)      { t.ax = float3(0, 0, 1); t.ay = float3(0, 1, 0); }
    else                      { t.ax = float3(1, 0, 0); t.ay = float3(0, 0, 1); }
    float2 p = float2(dot(wp, t.ax), dot(wp, t.ay));
    t.cell = floor(p / kTexel);
    t.uv = (t.cell + 0.5) * kTexel;
    t.centre = wp + t.ax * (t.uv.x - p.x) + t.ay * (t.uv.y - p.y);
    return t;
}

// Bands spaced evenly in stops, so the dark cut face gets as many steps as the
// lit bore, joined by an ordered dither on the texel grid. The dither covers
// only the edge of each band; a full-width one turns the whole face to static.
// Only brightness is stepped: each material keeps its own hue.
float3 rampQuantise(float3 c, float2 cell)
{
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    if (l < 1e-5) return c;
    int2 q = int2(cell) & 3;
    float threshold = 0.5 + ((kBayer[q.y * 4 + q.x] + 0.5) / 16.0 - 0.5) * kDitherSpan;
    float band = floor(log2(l) * kRampSteps + threshold);
    return c * (exp2(band / kRampSteps) / l);
}

float4 main(VSOut i) : SV_TARGET
{
    float3 n = normalize(i.nrm);

    const float3 keyDir  = normalize(float3(-0.40, 0.75, -0.55));
    const float3 fillDir = normalize(float3( 0.65,-0.35, -0.60));
    float3 keyCol  = float3(0.85, 0.95, 1.25);
    float3 fillCol = float3(0.32, 0.16, 0.55);

    float3 albedo = i.col * gTint.rgb;

    // Rock grain. The height field is sampled every couple of metres, so the
    // only place a boulder-sized surface can get metre-sized relief is here:
    // the gradient of the noise tilts the normal, the value itself mottles the
    // albedo, and the two together turn a facet into stone. It is read once
    // per texel, at the texel's centre, and so is all the lighting below.
    float grain = gObject.z;
    bool rock = grain > 0.001;
    float3 wp = i.wp;
    float2 cell = 0;
    if (rock)
    {
        Texel t = rockTexel(i.wp, n);
        wp = t.centre;
        cell = t.cell;
        const float e = 0.45;
        float2 q = t.uv * 0.55;
        float g0 = rockGrain(q);
        float gx = rockGrain(q + float2(e, 0));
        float gy = rockGrain(q + float2(0, e));
        n = normalize(n + (t.ax * (g0 - gx) + t.ay * (g0 - gy)) * grain * 3.4);
        albedo *= 0.68 + 0.64 * g0;
    }
    float3 v = normalize(gCamPos.xyz - wp);

    float ndl = saturate(dot(n, keyDir));
    float ndf = saturate(dot(n, fillDir)) * 0.6;
    float3 lit = albedo * (keyCol * ndl + fillCol * ndf + float3(0.05, 0.06, 0.10));

    // Rim / fresnel edge - the thing that makes untextured geometry read well.
    float rim = pow(1.0 - saturate(dot(n, v)), 3.0);
    lit += albedo * rim * float3(0.55, 0.95, 1.35) * 0.9;

    // Made things are allowed a highlight; rock never gets one. That single
    // contrast is what separates the two vocabularies at a glance.
    float gloss = gObject.y;
    if (gloss > 0.001)
    {
        float3 h = normalize(keyDir + v);
        lit += keyCol * pow(saturate(dot(n, h)), lerp(10.0, 90.0, gloss)) * gloss * 1.3;
    }

    lit += albedo * dynamicLights(wp, n);

    // Procedural strata: no textures, just world-space banding.
    float detail = gEmissive.a;
    float pulse = 0.75 + 0.25 * sin(gCamPos.w * 2.2 + wp.x * 0.05 + wp.y * 0.05);
    float3 glow;
    if (detail > 0.001)
    {
        float band = sin(wp.y * 0.55 + sin(wp.x * 0.13) * 2.0);
        float veins = smoothstep(0.86, 1.0, band);
        lit += albedo * veins * detail * float3(0.30, 0.55, 0.85);
        lit *= 1.0 + 0.10 * detail * sin(wp.x * 0.31 + wp.y * 0.17);

        // The plasma itself: full strength across the whole rock face, with only
        // a gentle very-low-frequency swell so it breathes rather than pools.
        if (gPlasma.x > 0.001)
        {
            float3 pal = plasmaField(wp.xy);
            float swell = 0.72 + 0.28 * sin(wp.x * 0.013 - wp.y * 0.009 + gCamPos.w * 0.21);
            lit += pal * pal * gPlasma.x * detail * swell;
        }

        // The mesh builder ramps glow to exactly 1 on the rock/void isoline and
        // down to 0 a fixed distance inside, so shaping it here gives one crisp
        // contour with a soft wash bleeding in behind it. The wash belongs to
        // the stone and is banded with it; the contour is kept apart.
        float g = saturate(i.glow);
        lit += gEmissive.rgb * (g * g * 0.70) * pulse;
        glow = gEmissive.rgb * smoothstep(0.86, 1.0, g) * 1.50 * pulse;
    }
    else
    {
        // Props carry a constant per-vertex glow: leave it linear.
        glow = gEmissive.rgb * i.glow * pulse;
    }

    // Meltdown wash. It scales surface and glow alike, so the two can stay
    // apart until the stone has been through its ramp.
    float melt = gParams.y;
    float3 meltK = lerp(1.0.xxx, float3(1.9, 0.42, 0.30), melt);
    float torch = torchFactor(wp);
    lit *= meltK * torch;
    glow = (glow * meltK + float3(0.16, 0.0, 0.0) * i.glow * melt) * torch;

    // The boundary contour is the line the player flies against, so it is
    // never banded or dithered: its constant width is the collision contract.
    if (rock && i.glow < 0.86) lit = rampQuantise(lit, cell);
    lit += glow;

    // Depth haze so the extruded caverns read as solid volume. It fades to the
    // planet's own sky rather than a fixed grey, so the bottom of a bore and
    // the void beyond the horizon are the same colour and the slice reads as
    // one object sitting in one place.
    float dist = length(gCamPos.xyz - wp);
    float fog = saturate(1.0 - exp(-dist * gParams.w));
    lit = lerp(lit, gSky.rgb * 1.5, fog * 0.85);

    return float4(lit, gTint.a);
}
)";

// -------------------------------------------------------------- glowing line
static const char* kLineVS = R"(
struct VSIn {
    float3 pos : POSITION;
    float3 uvh : TEXCOORD0;   // x = across, y = along, z = halfLen / halfWidth
    float4 col : COLOR;
};
struct VSOut {
    float4 svp : SV_POSITION;
    float3 uvh : TEXCOORD0;
    float4 col : COLOR;
    float3 wp  : TEXCOORD1;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.svp = mul(gViewProj, float4(i.pos, 1));
    o.uvh = i.uvh;
    o.col = i.col;
    o.wp = i.pos;
    return o;
}
)";

static const char* kLinePS = R"(
struct VSOut {
    float4 svp : SV_POSITION;
    float3 uvh : TEXCOORD0;
    float4 col : COLOR;
    float3 wp  : TEXCOORD1;
};

float4 main(VSOut i) : SV_TARGET
{
    float u = i.uvh.x;
    float t = i.uvh.y;
    float hl = max(i.uvh.z, 1.0001);
    float y = max(abs(t) * hl - (hl - 1.0), 0.0);
    float d = sqrt(u * u + y * y);
    float a = saturate(1.0 - d);
    float g = pow(a, 2.5) * 0.45 + pow(a, 10.0) * 1.5;
    return float4(emitColour(i.col.rgb * g * i.col.a), 1);
}
)";

// ---------------------------------------------------------- glow particles
static const char* kGlowVS = R"(
struct VSIn {
    float4 posSize : TEXCOORD0;   // xyz = world pos, w = size
    float4 col     : TEXCOORD1;   // rgb = HDR colour, a = intensity
    float4 shape   : TEXCOORD2;   // x = rot, y = type, z = stretch, w = spin phase
    uint   vid     : SV_VertexID;
};
struct VSOut {
    float4 svp   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR;
    float2 shape : TEXCOORD1;     // x = type, y = phase
};

VSOut main(VSIn i)
{
    float2 c = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    float2 s = float2(c.x * (1.0 + i.shape.z), c.y);
    float sa = sin(i.shape.x), ca = cos(i.shape.x);
    float2 r = float2(s.x * ca - s.y * sa, s.x * sa + s.y * ca);

    float3 wp = i.posSize.xyz + gCamRight.xyz * r.x * i.posSize.w
                              + gCamUp.xyz    * r.y * i.posSize.w;

    VSOut o;
    o.svp = mul(gViewProj, float4(wp, 1));
    o.uv = c;
    o.col = i.col;
    o.shape = float2(i.shape.y, i.shape.w);
    return o;
}
)";

static const char* kGlowPS = R"(
struct VSOut {
    float4 svp   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR;
    float2 shape : TEXCOORD1;
};

float4 main(VSOut i) : SV_TARGET
{
    float d = length(i.uv);
    int type = (int)(i.shape.x + 0.5);
    float a = 0;

    if (type == 1)
    {
        // Ring / shock puff.
        a = exp(-pow((d - 0.62) * 5.5, 2.0)) * 1.3;
    }
    else if (type == 2)
    {
        // Four point star flare.
        float ang = atan2(i.uv.y, i.uv.x) + i.shape.y;
        float spikes = 0.55 + 0.45 * pow(abs(cos(ang * 2.0)), 6.0);
        a = pow(saturate(1.0 - d), 2.0) * 0.4 + pow(saturate(1.0 - d / spikes), 6.0) * 1.2;
    }
    else
    {
        float b = saturate(1.0 - d);
        a = pow(b, 2.2) * 0.55 + pow(b, 9.0) * 1.35;
    }

    return float4(emitColour(i.col.rgb * a * i.col.a), 1);
}
)";

// ------------------------------------------------------------ debris shards
static const char* kShardVS = R"(
struct VSIn {
    float3 pos  : POSITION;
    float3 nrm  : NORMAL;
    float4 ipos : TEXCOORD0;   // xyz = pos, w = size
    float4 iq   : TEXCOORD1;   // rotation quaternion
    float4 icol : TEXCOORD2;
};
struct VSOut {
    float4 svp : SV_POSITION;
    float3 nrm : TEXCOORD0;
    float4 col : COLOR;
};

float3 qrot(float4 q, float3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

VSOut main(VSIn i)
{
    float3 lp = qrot(i.iq, i.pos * i.ipos.w);
    VSOut o;
    o.svp = mul(gViewProj, float4(i.ipos.xyz + lp, 1));
    o.nrm = qrot(i.iq, i.nrm);
    o.col = i.icol;
    return o;
}
)";

static const char* kShardPS = R"(
struct VSOut {
    float4 svp : SV_POSITION;
    float3 nrm : TEXCOORD0;
    float4 col : COLOR;
};

float4 main(VSOut i) : SV_TARGET
{
    float3 n = normalize(i.nrm);
    float f = saturate(dot(n, normalize(float3(-0.3, 0.7, -0.6)))) * 0.8 + 0.35;
    return float4(i.col.rgb * f * i.col.a, 1);
}
)";

// ------------------------------------------------------------ HUD panels
// The only alpha blended pipeline in the game: a soft edged slab dropped behind
// HUD text so the strokes are not fighting the cavern for contrast.
static const char* kPanelVS = R"(
struct VSIn {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;   // -1..1 across the panel
    float4 col : COLOR;
};
struct VSOut {
    float4 svp : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.svp = mul(gViewProj, float4(i.pos, 1));
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
)";

static const char* kPanelPS = R"(
struct VSOut {
    float4 svp : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR;
};

float4 main(VSOut i) : SV_TARGET
{
    float2 d = abs(i.uv);
    // Narrow ramp: enough to avoid a hard box, not so wide that text near the
    // edge of the slab loses its backing.
    float fade = 1.0 - smoothstep(0.85, 1.0, max(d.x, d.y));
    float sheen = 0.85 + 0.15 * i.uv.y;
    float scan = 0.94 + 0.06 * sin(i.svp.y * 0.55);
    return float4(i.col.rgb * sheen * scan, i.col.a * fade);
}
)";

// ------------------------------------------------------------ post process
static const char* kPostCommon = R"(
cbuffer PostCB : register(b0)
{
    float4 gTexel;     // xy = 1/size, zw = size
    float4 gBlur;      // xy = direction, z = threshold, w = intensity
    float4 gShock[4];  // xy = uv centre, z = radius, w = strength
    float4 gPostA;     // x = time, y = aberration, z = vignette, w = grain
    float4 gFlash;     // rgb = colour, a = amount
    float4 gMix;       // xyz = bloom weights, w = exposure
};

Texture2D    tex0 : register(t0);
Texture2D    tex1 : register(t1);
Texture2D    tex2 : register(t2);
Texture2D    tex3 : register(t3);
SamplerState smp  : register(s0);

struct FSOut { float4 svp : SV_POSITION; float2 uv : TEXCOORD0; };

// Dave Hoskins' "Hash without Sine", as vhash in the scene shaders.
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 acesTonemap(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
)";

static const char* kFullscreenVS = R"(
struct FSOut { float4 svp : SV_POSITION; float2 uv : TEXCOORD0; };

FSOut main(uint id : SV_VertexID)
{
    FSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.svp = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

static const char* kBrightPS = R"(
float4 main(FSOut i) : SV_TARGET
{
    float3 c = tex0.SampleLevel(smp, i.uv, 0).rgb;
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    float k = max(l - gBlur.z, 0.0) / max(l, 0.0001);
    return float4(c * k * gBlur.w, 1);
}
)";

static const char* kBlurPS = R"(
float4 main(FSOut i) : SV_TARGET
{
    float2 d = gBlur.xy * gTexel.xy;
    const float w0 = 0.227027, w1 = 0.1945946, w2 = 0.1216216, w3 = 0.054054, w4 = 0.016216;
    float3 c = tex0.SampleLevel(smp, i.uv, 0).rgb * w0;
    c += tex0.SampleLevel(smp, i.uv + d * 1.3846, 0).rgb * (w1 + w2);
    c += tex0.SampleLevel(smp, i.uv - d * 1.3846, 0).rgb * (w1 + w2);
    c += tex0.SampleLevel(smp, i.uv + d * 3.2308, 0).rgb * (w3 + w4);
    c += tex0.SampleLevel(smp, i.uv - d * 3.2308, 0).rgb * (w3 + w4);
    return float4(c, 1);
}
)";

static const char* kCopyPS = R"(
float4 main(FSOut i) : SV_TARGET
{
    return float4(tex0.SampleLevel(smp, i.uv, 0).rgb, 1);
}
)";

static const char* kCompositePS = R"(
float4 main(FSOut i) : SV_TARGET
{
    float aspect = gTexel.z / gTexel.w;
    float2 uv = i.uv;

    // Expanding shock rings warp the image - explosions push the world about.
    float ripple = 0;
    [unroll] for (int s = 0; s < 4; ++s)
    {
        if (gShock[s].w <= 0.0001) continue;
        float2 dv = (uv - gShock[s].xy) * float2(aspect, 1);
        float d = length(dv);
        float ring = exp(-pow((d - gShock[s].z) * 22.0, 2.0)) * gShock[s].w;
        uv += normalize(dv + 1e-5) * ring * 0.035;
        ripple += ring;
    }

    // Radial chromatic aberration, stronger toward the edges.
    float2 fromC = uv - 0.5;
    float ca = (gPostA.y * 0.0035 + ripple * 0.02) * (0.35 + dot(fromC, fromC) * 3.0);
    float3 col;
    col.r = tex0.SampleLevel(smp, uv + fromC * ca, 0).r;
    col.g = tex0.SampleLevel(smp, uv, 0).g;
    col.b = tex0.SampleLevel(smp, uv - fromC * ca, 0).b;

    float3 b0 = tex1.SampleLevel(smp, uv, 0).rgb;
    float3 b1 = tex2.SampleLevel(smp, uv, 0).rgb;
    float3 b2 = tex3.SampleLevel(smp, uv, 0).rgb;
    col += b0 * gMix.x + b1 * gMix.y + b2 * gMix.z;

    col += gFlash.rgb * gFlash.a;
    col *= gMix.w;
    col = acesTonemap(col);

    // Vignette + phosphor scanlines + grain: CRT memory, not a filter stack.
    float vig = 1.0 - gPostA.z * dot(fromC, fromC) * 1.15;
    col *= saturate(vig);
    // Fixed line count, so the grille stays a look instead of turning into a
    // per pixel screen door once the backbuffer is a high DPI one.
    float scan = 0.96 + 0.04 * sin(i.uv.y * 900.0 * 3.14159);
    col *= lerp(1.0, scan, 0.35);
    col += (hash12(i.uv * gTexel.zw + gPostA.x * 60.0) - 0.5) * gPostA.w * 0.05;

    return float4(max(col, 0), 1);
}
)";

}  // namespace shaders
}  // namespace zp

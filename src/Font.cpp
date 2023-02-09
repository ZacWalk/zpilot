#include "Font.h"

namespace zp {

struct Stroke {
    float x0, y0, x1, y1;
};

#define G(name, ...) static const Stroke name[] = {__VA_ARGS__};

G(gA, {0, 0, .5f, 1}, {.5f, 1, 1, 0}, {.15f, .3f, .85f, .3f})
G(gB, {0, 0, 0, 1}, {0, 1, .8f, 1}, {.8f, 1, 1, .8f}, {1, .8f, .8f, .55f}, {.8f, .55f, 0, .55f},
  {.8f, .55f, 1, .3f}, {1, .3f, .8f, 0}, {.8f, 0, 0, 0})
G(gC, {1, .85f, .75f, 1}, {.75f, 1, .25f, 1}, {.25f, 1, 0, .75f}, {0, .75f, 0, .25f},
  {0, .25f, .25f, 0}, {.25f, 0, .75f, 0}, {.75f, 0, 1, .15f})
G(gD, {0, 0, 0, 1}, {0, 1, .7f, 1}, {.7f, 1, 1, .7f}, {1, .7f, 1, .3f}, {1, .3f, .7f, 0},
  {.7f, 0, 0, 0})
G(gE, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, .5f, .7f, .5f})
G(gF, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, .55f, .7f, .55f})
G(gG, {1, .85f, .75f, 1}, {.75f, 1, .25f, 1}, {.25f, 1, 0, .75f}, {0, .75f, 0, .25f},
  {0, .25f, .25f, 0}, {.25f, 0, .8f, 0}, {.8f, 0, 1, .2f}, {1, .2f, 1, .45f}, {1, .45f, .55f, .45f})
G(gH, {0, 0, 0, 1}, {1, 0, 1, 1}, {0, .5f, 1, .5f})
G(gI, {.2f, 1, .8f, 1}, {.5f, 1, .5f, 0}, {.2f, 0, .8f, 0})
G(gJ, {.8f, 1, .8f, .25f}, {.8f, .25f, .6f, 0}, {.6f, 0, .25f, 0}, {.25f, 0, 0, .25f})
G(gK, {0, 0, 0, 1}, {1, 1, 0, .45f}, {.35f, .62f, 1, 0})
G(gL, {0, 1, 0, 0}, {0, 0, 1, 0})
G(gM, {0, 0, 0, 1}, {0, 1, .5f, .45f}, {.5f, .45f, 1, 1}, {1, 1, 1, 0})
G(gN, {0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 1, 1})
G(gO, {.25f, 1, .75f, 1}, {.75f, 1, 1, .75f}, {1, .75f, 1, .25f}, {1, .25f, .75f, 0},
  {.75f, 0, .25f, 0}, {.25f, 0, 0, .25f}, {0, .25f, 0, .75f}, {0, .75f, .25f, 1})
G(gP, {0, 0, 0, 1}, {0, 1, .8f, 1}, {.8f, 1, 1, .8f}, {1, .8f, .8f, .55f}, {.8f, .55f, 0, .55f})
G(gQ, {.25f, 1, .75f, 1}, {.75f, 1, 1, .75f}, {1, .75f, 1, .25f}, {1, .25f, .75f, 0},
  {.75f, 0, .25f, 0}, {.25f, 0, 0, .25f}, {0, .25f, 0, .75f}, {0, .75f, .25f, 1},
  {.6f, .28f, 1, -.05f})
G(gR, {0, 0, 0, 1}, {0, 1, .8f, 1}, {.8f, 1, 1, .8f}, {1, .8f, .8f, .55f}, {.8f, .55f, 0, .55f},
  {.45f, .55f, 1, 0})
G(gS, {1, .85f, .75f, 1}, {.75f, 1, .25f, 1}, {.25f, 1, 0, .8f}, {0, .8f, .2f, .56f},
  {.2f, .56f, .8f, .5f}, {.8f, .5f, 1, .25f}, {1, .25f, .75f, 0}, {.75f, 0, .25f, 0},
  {.25f, 0, 0, .15f})
G(gT, {0, 1, 1, 1}, {.5f, 1, .5f, 0})
G(gU, {0, 1, 0, .25f}, {0, .25f, .25f, 0}, {.25f, 0, .75f, 0}, {.75f, 0, 1, .25f}, {1, .25f, 1, 1})
G(gV, {0, 1, .5f, 0}, {.5f, 0, 1, 1})
G(gW, {0, 1, .2f, 0}, {.2f, 0, .5f, .6f}, {.5f, .6f, .8f, 0}, {.8f, 0, 1, 1})
G(gX, {0, 0, 1, 1}, {0, 1, 1, 0})
G(gY, {0, 1, .5f, .5f}, {1, 1, .5f, .5f}, {.5f, .5f, .5f, 0})
G(gZ, {0, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0})

G(g0, {.25f, 1, .75f, 1}, {.75f, 1, 1, .75f}, {1, .75f, 1, .25f}, {1, .25f, .75f, 0},
  {.75f, 0, .25f, 0}, {.25f, 0, 0, .25f}, {0, .25f, 0, .75f}, {0, .75f, .25f, 1},
  {.15f, .2f, .85f, .8f})
G(g1, {.2f, .8f, .5f, 1}, {.5f, 1, .5f, 0}, {.2f, 0, .8f, 0})
G(g2, {0, .8f, .25f, 1}, {.25f, 1, .75f, 1}, {.75f, 1, 1, .75f}, {1, .75f, 0, 0}, {0, 0, 1, 0})
G(g3, {0, 1, 1, 1}, {1, 1, .5f, .52f}, {.5f, .52f, 1, .28f}, {1, .28f, .75f, 0},
  {.75f, 0, .25f, 0}, {.25f, 0, 0, .15f})
G(g4, {.75f, 0, .75f, 1}, {.75f, 1, 0, .35f}, {0, .35f, 1, .35f})
G(g5, {1, 1, 0, 1}, {0, 1, 0, .55f}, {0, .55f, .7f, .55f}, {.7f, .55f, 1, .35f}, {1, .35f, 1, .2f},
  {1, .2f, .75f, 0}, {.75f, 0, .2f, 0}, {.2f, 0, 0, .15f})
G(g6, {.9f, .9f, .6f, 1}, {.6f, 1, .2f, 1}, {.2f, 1, 0, .7f}, {0, .7f, 0, .25f},
  {0, .25f, .25f, 0}, {.25f, 0, .7f, 0}, {.7f, 0, 1, .25f}, {1, .25f, .7f, .5f}, {.7f, .5f, 0, .5f})
G(g7, {0, 1, 1, 1}, {1, 1, .35f, 0})
G(g8, {.25f, 1, .75f, 1}, {.75f, 1, 1, .75f}, {1, .75f, 1, .25f}, {1, .25f, .75f, 0},
  {.75f, 0, .25f, 0}, {.25f, 0, 0, .25f}, {0, .25f, 0, .75f}, {0, .75f, .25f, 1},
  {.05f, .5f, .95f, .5f})
G(g9, {.1f, .1f, .4f, 0}, {.4f, 0, .8f, 0}, {.8f, 0, 1, .3f}, {1, .3f, 1, .75f}, {1, .75f, .75f, 1},
  {.75f, 1, .3f, 1}, {.3f, 1, 0, .75f}, {0, .75f, .3f, .5f}, {.3f, .5f, 1, .5f})

G(gDot, {.38f, .02f, .62f, .12f})
G(gComma, {.55f, .14f, .3f, -.12f})
G(gColon, {.38f, .62f, .62f, .62f}, {.38f, .18f, .62f, .18f})
G(gDash, {.1f, .5f, .9f, .5f})
G(gPlus, {.1f, .5f, .9f, .5f}, {.5f, .12f, .5f, .88f})
G(gSlash, {0, 0, 1, 1})
G(gBang, {.5f, 1, .5f, .3f}, {.5f, .12f, .5f, .02f})
G(gQuery, {0, .8f, .25f, 1}, {.25f, 1, .7f, 1}, {.7f, 1, .95f, .75f}, {.95f, .75f, .5f, .45f},
  {.5f, .45f, .5f, .3f}, {.5f, .14f, .5f, .04f})
G(gPct, {0, 0, 1, 1}, {0, .78f, .22f, 1}, {.22f, 1, 0, .78f}, {.78f, 0, 1, .22f}, {1, .22f, .78f, 0})
G(gLP, {.7f, 1, .3f, .7f}, {.3f, .7f, .3f, .3f}, {.3f, .3f, .7f, 0})
G(gRP, {.3f, 1, .7f, .7f}, {.7f, .7f, .7f, .3f}, {.7f, .3f, .3f, 0})
G(gLT, {.8f, 1, .2f, .5f}, {.2f, .5f, .8f, 0})
G(gGT, {.2f, 1, .8f, .5f}, {.8f, .5f, .2f, 0})
G(gStar, {.5f, .15f, .5f, .85f}, {.12f, .32f, .88f, .68f}, {.88f, .32f, .12f, .68f})
G(gEq, {.1f, .66f, .9f, .66f}, {.1f, .34f, .9f, .34f})
G(gUnd, {0, 0, 1, 0})

#undef G

struct Glyph {
    const Stroke* s;
    int n;
};

static Glyph LookupGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case 'A': return {gA, _countof(gA)};
        case 'B': return {gB, _countof(gB)};
        case 'C': return {gC, _countof(gC)};
        case 'D': return {gD, _countof(gD)};
        case 'E': return {gE, _countof(gE)};
        case 'F': return {gF, _countof(gF)};
        case 'G': return {gG, _countof(gG)};
        case 'H': return {gH, _countof(gH)};
        case 'I': return {gI, _countof(gI)};
        case 'J': return {gJ, _countof(gJ)};
        case 'K': return {gK, _countof(gK)};
        case 'L': return {gL, _countof(gL)};
        case 'M': return {gM, _countof(gM)};
        case 'N': return {gN, _countof(gN)};
        case 'O': return {gO, _countof(gO)};
        case 'P': return {gP, _countof(gP)};
        case 'Q': return {gQ, _countof(gQ)};
        case 'R': return {gR, _countof(gR)};
        case 'S': return {gS, _countof(gS)};
        case 'T': return {gT, _countof(gT)};
        case 'U': return {gU, _countof(gU)};
        case 'V': return {gV, _countof(gV)};
        case 'W': return {gW, _countof(gW)};
        case 'X': return {gX, _countof(gX)};
        case 'Y': return {gY, _countof(gY)};
        case 'Z': return {gZ, _countof(gZ)};
        case '0': return {g0, _countof(g0)};
        case '1': return {g1, _countof(g1)};
        case '2': return {g2, _countof(g2)};
        case '3': return {g3, _countof(g3)};
        case '4': return {g4, _countof(g4)};
        case '5': return {g5, _countof(g5)};
        case '6': return {g6, _countof(g6)};
        case '7': return {g7, _countof(g7)};
        case '8': return {g8, _countof(g8)};
        case '9': return {g9, _countof(g9)};
        case '.': return {gDot, _countof(gDot)};
        case ',': return {gComma, _countof(gComma)};
        case ':': return {gColon, _countof(gColon)};
        case '-': return {gDash, _countof(gDash)};
        case '+': return {gPlus, _countof(gPlus)};
        case '/': return {gSlash, _countof(gSlash)};
        case '!': return {gBang, _countof(gBang)};
        case '?': return {gQuery, _countof(gQuery)};
        case '%': return {gPct, _countof(gPct)};
        case '(': return {gLP, _countof(gLP)};
        case ')': return {gRP, _countof(gRP)};
        case '<': return {gLT, _countof(gLT)};
        case '>': return {gGT, _countof(gGT)};
        case '*': return {gStar, _countof(gStar)};
        case '=': return {gEq, _countof(gEq)};
        case '_': return {gUnd, _countof(gUnd)};
        default: return {nullptr, 0};
    }
}

float GlyphTextWidth(const char* text, float size, float spacing) {
    return (float)strlen(text) * size * spacing;
}

void DrawGlyphText(Renderer& r, const char* text, Vec2 topLeft, float size, Vec4 color,
                   float thickness, float spacing) {
    // Single pixel strokes disappear over bright rock and bloom. Every glyph
    // gets a fixed extra half width on top of whatever the caller asked for.
    thickness = thickness * 1.25f + 0.95f;
    float x = topLeft.x;
    const float w = size * (spacing - 0.14f);
    for (const char* p = text; *p; ++p, x += size * spacing) {
        Glyph g = LookupGlyph(*p);
        if (!g.s) continue;
        for (int i = 0; i < g.n; ++i) {
            const Stroke& s = g.s[i];
            Vec3 a(x + s.x0 * w, topLeft.y + (1.0f - s.y0) * size, 0);
            Vec3 b(x + s.x1 * w, topLeft.y + (1.0f - s.y1) * size, 0);
            r.AddLine(a, b, thickness, color);
        }
    }
}

void DrawGlyphTextWorld(Renderer& r, const char* text, Vec2 centre, float z, float size,
                        Vec4 color, float thickness, float spacing) {
    float x = centre.x - GlyphTextWidth(text, size, spacing) * 0.5f;
    const float w = size * (spacing - 0.14f);
    for (const char* p = text; *p; ++p, x += size * spacing) {
        Glyph g = LookupGlyph(*p);
        if (!g.s) continue;
        for (int i = 0; i < g.n; ++i) {
            const Stroke& s = g.s[i];
            Vec3 a(x + s.x0 * w, centre.y + (s.y0 - 0.5f) * size, z);
            Vec3 b(x + s.x1 * w, centre.y + (s.y1 - 0.5f) * size, z);
            r.AddLine(a, b, thickness, color);
        }
    }
}

void DrawGlyphTextCentred(Renderer& r, const char* text, Vec2 centreTop, float size, Vec4 color,
                          float thickness, float spacing) {
    float w = GlyphTextWidth(text, size, spacing);
    DrawGlyphText(r, text, {centreTop.x - w * 0.5f, centreTop.y}, size, color, thickness, spacing);
}

}  // namespace zp

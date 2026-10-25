// zpilot - stroke vector font. Glyphs are line segments, drawn through the
// same glow pipeline as everything else.
#pragma once
#include "Renderer.h"

namespace zp {

void DrawGlyphText(Renderer& r, const char* text, Vec2 topLeft, float size, Vec4 color,
                   float thickness = 1.4f, float spacing = 0.70f);
void DrawGlyphTextCentred(Renderer& r, const char* text, Vec2 centreTop, float size, Vec4 color,
                          float thickness = 1.4f, float spacing = 0.70f);
// World space, y up, centred on `centre`. Used for the stencilled labels on
// the cavern props.
void DrawGlyphTextWorld(Renderer& r, const char* text, Vec2 centre, float z, float size,
                        Vec4 color, float thickness = 0.10f, float spacing = 0.70f);
float GlyphTextWidth(const char* text, float size, float spacing = 0.70f);

}  // namespace zp

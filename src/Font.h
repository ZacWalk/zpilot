// zpilot - stroke vector font. Glyphs are line segments, drawn as crisp quads
// over a hard drop shadow, the way a 16-bit title card was lettered.
#pragma once
#include "Renderer.h"

namespace zp
{

	// Screen space, y down, from the top left of the first glyph. Text on a panel
	// needs no shadow; text laid straight over the world takes one.
	void DrawGlyphText(Renderer& r, const char* text, Vec2 topLeft, float size, Vec4 colour, float thickness = 1.4f,
	                   float spacing = 0.70f, bool shadow = false);
	void DrawGlyphTextCentred(Renderer& r, const char* text, Vec2 centreTop, float size, Vec4 colour,
	                          float thickness = 1.4f, float spacing = 0.70f, bool shadow = false);
	// Demo-scene chrome: a copper sky over a gold horizon across the letters, cut
	// out of a dark outline.
	void DrawChromeTextCentred(Renderer& r, const char* text, Vec2 centreTop, float size, float thickness,
	                           float spacing = 0.78f);
	// World space, y up, centred on `centre`. The stencilled labels on the props.
	void DrawGlyphTextWorld(Renderer& r, const char* text, Vec2 centre, float size, Vec4 colour,
	                        float thickness = 0.10f, float spacing = 0.70f);
	float GlyphTextWidth(const char* text, float size, float spacing = 0.70f);

} // namespace zp

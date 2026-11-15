// zpilot - the 2D renderer. A camera that pans and zooms over the play plane
// but never rotates, and the few primitives the game draws everything with,
// all through libdesktop's draw_context: crisp polygons, gradients, glows,
// outlines and blocks.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <libdesktop/render.h>

#include "Common.h"

namespace zp
{

	// An Amiga's palette registers held four bits a channel. Solid colours are
	// snapped to that grid, which is most of what gives the screen its era.
	Vec4 Ocs(float r, float g, float b, float a = 1.0f);
	Vec4 Ocs(Vec3 c, float a = 1.0f);
	// 0xRRGGBB, snapped the same way.
	Vec4 Hex(uint32_t rgb, float a = 1.0f);
	Vec4 Mix(Vec4 a, Vec4 b, float t);
	Vec4 Scale(Vec4 c, float k); // brightness, alpha kept
	Vec4 WithAlpha(Vec4 c, float a);
	// Effects are authored brighter than white; this rolls them into range, the
	// hottest going white rather than clipping one channel at a time.
	Vec4 Hot(Vec3 hdr, float a = 1.0f);

	// What fills a shape. Its points are in the space the shape is given in -
	// world units for the world calls, pixels for the Screen ones - and the
	// camera carries them with the shape.
	struct Fill
	{
		enum Kind : uint8_t
		{
			SOLID,
			LINEAR,
			RADIAL
		};
		Kind kind = SOLID;
		Vec4 a, b;
		Vec2 from, to;
		float radius = 0;

		static Fill Solid(Vec4 c);
		static Fill Linear(Vec2 from, Vec4 a, Vec2 to, Vec4 b);
		static Fill Radial(Vec2 centre, float radius, Vec4 inner, Vec4 outer);
	};

	class Renderer
	{
	public:
		// Half the view's height per unit of distance between the camera and the
		// play plane: a 50 degree lens.
		static constexpr float kViewSlope = 0.4642f;

		void Begin(render::draw_context& dc, core::recti bounds);
		void End();

		float Width() const { return (float)bounds_.width(); }
		float Height() const { return (float)bounds_.height(); }

		// The camera stands `distance` in front of the plane, centred on `centre`.
		// Distance only zooms the plane; it also sets how much slower things
		// behind the plane slide by.
		void SetCamera(Vec2 centre, float distance);
		Vec2 CameraCentre() const { return cam_; }
		float Scale() const { return scale_; } // pixels per world unit on the plane
		Vec2 ToScreen(Vec2 world) const
		{
			return {half_.x + (world.x - cam_.x) * scale_, half_.y - (world.y - cam_.y) * scale_};
		}
		// A point `depth` units behind the plane (in front of it if negative), in
		// perspective. Depths stay well inside the camera's distance.
		Vec2 ToScreen(Vec2 world, float depth) const;
		float ScaleAt(float depth) const { return scale_ * dist_ / (dist_ + depth); }
		Vec2 ToWorld(Vec2 screen) const
		{
			return {cam_.x + (screen.x - half_.x) / scale_, cam_.y - (screen.y - half_.y) / scale_};
		}
		// The plane's visible box, grown by margin world units.
		void ViewBox(float margin, Vec2* lo, Vec2* hi) const;
		bool Visible(Vec2 lo, Vec2 hi, float margin = 0.0f) const;

		// World space: points in world units, y up. Borders are in pixels, so an
		// outline stays one crisp pixel at every zoom.
		void Poly(std::span<const Vec2> pts, const Fill& fill, Vec4 border = {}, float borderPx = 0,
		          bool smooth = false);
		// A soft disc that fades to nothing at its rim.
		void Glow(Vec2 centre, float radius, Vec4 colour);
		void Ring(Vec2 centre, float radius, float widthPx, Vec4 colour, int segments = 28);
		void Line(Vec2 a, Vec2 b, float widthPx, Vec4 colour);
		void Block(Vec2 centre, float sizePx, Vec4 colour);
		// An axis aligned box, covering exactly the pixels a crisp polygon of the
		// same corners would, at a fifth of the cost.
		void Box(Vec2 lo, Vec2 hi, Vec4 colour);

		// Screen space: pixels from the canvas's top left, y down.
		void Rect(float x, float y, float w, float h, Vec4 colour);
		void ScreenPoly(std::span<const Vec2> pts, const Fill& fill, Vec4 border = {}, float borderPx = 0,
		                bool smooth = false);
		void ScreenLine(Vec2 a, Vec2 b, float width, Vec4 colour);
		void Clip(float x, float y, float w, float h);
		void Unclip();

	private:
		render::paint ToPaint(const Fill& f, bool world) const;
		core::pointd Out(Vec2 screen) const
		{
			return {bounds_.left + (double)screen.x, bounds_.top + (double)screen.y};
		}
		void Emit(const std::vector<core::pointd>& pts, const Fill& fill, bool world, Vec4 border, float borderPx,
		          bool smooth);

		render::draw_context* dc_ = nullptr;
		core::recti bounds_;
		Vec2 half_;
		Vec2 cam_;
		float dist_ = 50.0f;
		float scale_ = 1.0f;
		std::vector<core::pointd> pts_;
	};

	// A screenshot, encoded and published whole: a failed capture can never leave
	// a stale or half written file behind.
	bool SavePng(const core::surface& picture, const std::filesystem::path& path);

} // namespace zp

#include "Renderer.h"

#include <new>

#include <libfiles/encode.h>

namespace zp
{

	namespace
	{

		float Snap4(float v)
		{
			return std::round(saturatef(v) * 15.0f) / 15.0f;
		}

		core::color ToColour(Vec4 c)
		{
			return core::color(saturatef(c.x), saturatef(c.y), saturatef(c.z), saturatef(c.w));
		}

		core::file_path SharedPath(const std::filesystem::path& path)
		{
			const auto utf8 = path.u8string();
			return core::file_path(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
		}

		bool PublishPng(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
		{
			const core::file_path target = SharedPath(path);
			const core::folder_path folder = target.folder();
			if (!folder.is_empty())
			{
				const auto created = platform::create_folder(folder);
				if (created.failed() && !platform::is_directory(folder))
				{
					core::trace("zpilot: screenshot directory creation failed: " + created.error_message);
					return false;
				}
			}

			const auto opened = platform::create_atomic_file_writer_result(target);
			if (!opened.success())
			{
				core::trace("zpilot: screenshot staging failed: " + opened.result.error_message);
				return false;
			}
			const auto writer = opened.writer;
			const auto written = writer->write(std::span<const uint8_t>(bytes.data(), bytes.size()));
			if (written.failed())
			{
				core::trace("zpilot: screenshot staging write failed: " + written.error_message);
				return false;
			}
			const auto flushed = writer->flush();
			if (flushed.failed())
			{
				core::trace("zpilot: screenshot flush failed: " + flushed.error_message);
				return false;
			}
			const auto committed = writer->commit();
			if (committed.failed())
			{
				core::trace("zpilot: screenshot publication failed: " + committed.error_message);
				return false;
			}
			const auto published = platform::file_attributes(target);
			if (!published.exists())
			{
				if (published.presence == platform::file_presence::unknown)
					core::trace("zpilot: screenshot publication query failed");
				else core::trace("zpilot: screenshot publication reported success but file is missing");
				return false;
			}
			return true;
		}

	} // namespace

	Vec4 Ocs(float r, float g, float b, float a)
	{
		return {Snap4(r), Snap4(g), Snap4(b), saturatef(a)};
	}
	Vec4 Ocs(Vec3 c, float a)
	{
		return Ocs(c.x, c.y, c.z, a);
	}

	Vec4 Hex(uint32_t rgb, float a)
	{
		return Ocs(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
	}

	Vec4 Mix(Vec4 a, Vec4 b, float t)
	{
		return lerp(a, b, saturatef(t));
	}
	Vec4 Scale(Vec4 c, float k)
	{
		return {c.x * k, c.y * k, c.z * k, c.w};
	}
	Vec4 WithAlpha(Vec4 c, float a)
	{
		return {c.x, c.y, c.z, a};
	}

	Vec4 Hot(Vec3 hdr, float a)
	{
		auto roll = [](float v)
		{
			return 1.0f - expf(-1.1f * std::max(0.0f, v));
		};
		return {roll(hdr.x), roll(hdr.y), roll(hdr.z), saturatef(a)};
	}

	Fill Fill::Solid(Vec4 c)
	{
		Fill f;
		f.a = f.b = c;
		return f;
	}

	Fill Fill::Linear(Vec2 from, Vec4 a, Vec2 to, Vec4 b)
	{
		Fill f;
		f.kind = LINEAR;
		f.a = a;
		f.b = b;
		f.from = from;
		f.to = to;
		return f;
	}

	Fill Fill::Radial(Vec2 centre, float radius, Vec4 inner, Vec4 outer)
	{
		Fill f;
		f.kind = RADIAL;
		f.a = inner;
		f.b = outer;
		f.from = centre;
		f.radius = radius;
		return f;
	}

	void Renderer::Begin(render::draw_context& dc, core::recti bounds)
	{
		dc_ = &dc;
		bounds_ = bounds;
		half_ = {bounds.width() * 0.5f, bounds.height() * 0.5f};
		SetCamera(cam_, dist_);
	}

	void Renderer::End()
	{
		dc_ = nullptr;
	}

	void Renderer::SetCamera(Vec2 centre, float distance)
	{
		cam_ = centre;
		dist_ = std::max(1.0f, distance);
		scale_ = std::max(1.0f, half_.y) / (dist_ * kViewSlope);
	}

	Vec2 Renderer::ToScreen(Vec2 world, float depth) const
	{
		const float k = ScaleAt(depth);
		return {half_.x + (world.x - cam_.x) * k, half_.y - (world.y - cam_.y) * k};
	}

	void Renderer::ViewBox(float margin, Vec2* lo, Vec2* hi) const
	{
		const float hw = half_.x / scale_ + margin, hh = half_.y / scale_ + margin;
		*lo = {cam_.x - hw, cam_.y - hh};
		*hi = {cam_.x + hw, cam_.y + hh};
	}

	bool Renderer::Visible(Vec2 lo, Vec2 hi, float margin) const
	{
		Vec2 vlo, vhi;
		ViewBox(margin, &vlo, &vhi);
		return hi.x >= vlo.x && lo.x <= vhi.x && hi.y >= vlo.y && lo.y <= vhi.y;
	}

	render::paint Renderer::ToPaint(const Fill& f, bool world) const
	{
		auto at = [&](Vec2 p)
		{
			return Out(world ? ToScreen(p) : p);
		};
		switch (f.kind)
		{
		case Fill::LINEAR: return render::paint::linear(at(f.from), ToColour(f.a), at(f.to), ToColour(f.b));
		case Fill::RADIAL:
			return render::paint::radial(at(f.from), f.radius * (world ? scale_ : 1.0f), ToColour(f.a), ToColour(f.b));
		default: return render::paint(ToColour(f.a));
		}
	}

	void Renderer::Emit(const std::vector<core::pointd>& pts, const Fill& fill, bool world, Vec4 border, float borderPx,
	                    bool smooth)
	{
		if (!dc_ || pts.size() < 3) return;
		render::polygon_style style;
		style.fill = ToPaint(fill, world);
		style.smooth = smooth;
		if (border.w > 0.0f && borderPx > 0.0f)
		{
			style.border = ToColour(border);
			style.border_width = borderPx;
		}
		dc_->draw_polygon(pts, style);
	}

	void Renderer::Poly(std::span<const Vec2> pts, const Fill& fill, Vec4 border, float borderPx, bool smooth)
	{
		pts_.clear();
		for (const Vec2& p : pts) pts_.push_back(Out(ToScreen(p)));
		Emit(pts_, fill, true, border, borderPx, smooth);
	}

	void Renderer::Glow(Vec2 centre, float radius, Vec4 colour)
	{
		if (radius <= 0.0f || colour.w <= 0.0f) return;
		const Vec2 c = ToScreen(centre);
		const float r = radius * scale_;
		if (c.x + r < 0 || c.y + r < 0 || c.x - r > Width() || c.y - r > Height()) return;
		// An octagon around the circle: the gradient is transparent past its rim.
		const float k = r * 1.0824f;
		pts_.clear();
		for (int i = 0; i < 8; ++i)
		{
			const float a = kTau * (i + 0.5f) / 8.0f;
			pts_.push_back(Out({c.x + cosf(a) * k, c.y + sinf(a) * k}));
		}
		Emit(pts_, Fill::Radial(c, r, colour, WithAlpha(colour, 0.0f)), false, {}, 0, false);
	}

	void Renderer::Ring(Vec2 centre, float radius, float widthPx, Vec4 colour, int segments)
	{
		if (radius <= 0.0f || colour.w <= 0.0f || widthPx <= 0.0f) return;
		const Vec2 c = ToScreen(centre);
		const float r = radius * scale_;
		if (c.x + r < -widthPx || c.y + r < -widthPx || c.x - r > Width() + widthPx || c.y - r > Height() + widthPx)
			return;
		pts_.clear();
		for (int i = 0; i < segments; ++i)
		{
			const float a = kTau * i / segments;
			pts_.push_back(Out({c.x + cosf(a) * r, c.y + sinf(a) * r}));
		}
		Emit(pts_, Fill::Solid({0, 0, 0, 0}), false, colour, widthPx, false);
	}

	void Renderer::Line(Vec2 a, Vec2 b, float widthPx, Vec4 colour)
	{
		ScreenLine(ToScreen(a), ToScreen(b), widthPx, colour);
	}

	void Renderer::Block(Vec2 centre, float sizePx, Vec4 colour)
	{
		const Vec2 c = ToScreen(centre);
		const float h = std::max(0.5f, sizePx * 0.5f);
		Rect(c.x - h, c.y - h, h * 2.0f, h * 2.0f, colour);
	}

	void Renderer::Box(Vec2 lo, Vec2 hi, Vec4 colour)
	{
		if (!dc_ || colour.w <= 0.0f) return;
		// A crisp polygon fills the pixels whose centres are inside it, its left and
		// top edges included; so does this, so the two tile without a seam.
		const Vec2 a = ToScreen(Vec2(lo.x, hi.y)), b = ToScreen(Vec2(hi.x, lo.y));
		const int l = (int)std::ceil(a.x - 0.5f), t = (int)std::ceil(a.y - 0.5f);
		const int r = (int)std::ceil(b.x - 0.5f), btm = (int)std::ceil(b.y - 0.5f);
		if (r <= l || btm <= t || r < 0 || btm < 0 || l > bounds_.width() || t > bounds_.height()) return;
		dc_->draw_rect(core::recti(bounds_.left + l, bounds_.top + t, bounds_.left + r, bounds_.top + btm),
		               ToColour(colour));
	}

	void Renderer::Rect(float x, float y, float w, float h, Vec4 colour)
	{
		if (!dc_ || colour.w <= 0.0f || w <= 0.0f || h <= 0.0f) return;
		const int l = (int)std::floor(x + 0.5f), t = (int)std::floor(y + 0.5f);
		const int r = std::max(l + 1, (int)std::floor(x + w + 0.5f));
		const int b = std::max(t + 1, (int)std::floor(y + h + 0.5f));
		if (r < 0 || b < 0 || l > bounds_.width() || t > bounds_.height()) return;
		dc_->draw_rect(core::recti(bounds_.left + l, bounds_.top + t, bounds_.left + r, bounds_.top + b),
		               ToColour(colour));
	}

	void Renderer::ScreenPoly(std::span<const Vec2> pts, const Fill& fill, Vec4 border, float borderPx, bool smooth)
	{
		pts_.clear();
		for (const Vec2& p : pts) pts_.push_back(Out(p));
		Emit(pts_, fill, false, border, borderPx, smooth);
	}

	void Renderer::ScreenLine(Vec2 a, Vec2 b, float width, Vec4 colour)
	{
		if (colour.w <= 0.0f || width <= 0.0f) return;
		Vec2 d = b - a;
		const float len = length(d);
		d = len > 1e-4f ? d / len : Vec2(1, 0);
		const float h = width * 0.5f;
		const Vec2 n = perp(d) * h, e = d * h;
		const Vec2 quad[4] = {a - e + n, b + e + n, b + e - n, a - e - n};
		ScreenPoly(quad, Fill::Solid(colour));
	}

	void Renderer::Clip(float x, float y, float w, float h)
	{
		if (!dc_) return;
		dc_->clip_bounds(core::recti(bounds_.left + (int)std::floor(x), bounds_.top + (int)std::floor(y),
		                             bounds_.left + (int)std::ceil(x + w), bounds_.top + (int)std::ceil(y + h)));
	}

	void Renderer::Unclip()
	{
		if (dc_) dc_->restore_clip();
	}

	bool SavePng(const core::surface& picture, const std::filesystem::path& path)
	{
		if (picture.empty()) return false;
		const auto format = picture.format();
		if (format != core::texture_format::ARGB && format != core::texture_format::RGB)
		{
			core::trace("zpilot: screenshot surface is not packed 32-bit");
			return false;
		}
		std::vector<uint8_t> png;
		try
		{
			// The fourth byte carries no colour: a window has no use for alpha.
			const libfiles::image_view image = {
				picture.pixels(), picture.stride(), picture.width(), picture.height(), libfiles::pixel_format::bgrx8,
			};
			auto encoded = libfiles::encode_png(image, 1, {});
			for (const auto& note : encoded.notes) core::trace("zpilot: PNG encoder: " + note);
			if (encoded.data.empty())
			{
				core::trace("zpilot: PNG encode returned no data");
				return false;
			}
			png = std::move(encoded.data);
		}
		catch (const libfiles::encode_error& error)
		{
			core::trace(std::string("zpilot: PNG encode failed: ") + error.what());
			return false;
		}
		catch (const std::bad_alloc&)
		{
			core::trace("zpilot: PNG encode ran out of memory");
			return false;
		}
		return PublishPng(path, png);
	}

} // namespace zp

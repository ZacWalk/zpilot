// zpilot - entry point and deterministic capture driver. The game runs in a
// libdesktop shell: one window, a fixed step, and a canvas it draws on.
#include "Game.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <string_view>

#include <libdesktop/platform.h>
#include <libdesktop/ui.h>
#include <libdesktop/ui/shell.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

using namespace zp;

namespace
{

	struct Beat
	{
		int frame;
		Key key;
		bool down;
	};

	// A key edge pair posted to the window as real key messages, the way a
	// keyboard delivers them.
	struct Tap
	{
		int frame;
		Key key;
	};

	struct Shot
	{
		int frame;
		const wchar_t* name;
		Mode mode;
		int planet = -1; // any
	};

	constexpr int kWrongShotExit = 100;
	constexpr int kDeepPlanet = 3;
	// The game steps in sixtieths of a second, in play and in the capture.
	constexpr double kStepsPerSecond = 60.0;
	// Capture shots are this size whatever the window or the screen's scale, so
	// every machine takes the same frames.
	constexpr core::sizei kShotExtent{1600, 900};
	// clang-format off
	const Beat kScript[] = {
		{80, KEY_START, true},    {82, KEY_START, false},
		{150, KEY_START, true},   {152, KEY_START, false},
		{225, KEY_START, true},   {227, KEY_START, false},
		{700, KEY_TRACTOR, true}, {770, KEY_TRACTOR, false},
		{800, KEY_SHIELD, true},  {840, KEY_SHIELD, false},
		{860, KEY_HELP, true},    {862, KEY_HELP, false},
		{900, KEY_HELP, true},    {902, KEY_HELP, false},
		{920, KEY_QUIT, true},    {922, KEY_QUIT, false},
		{930, KEY_QUIT, true},    {932, KEY_QUIT, false},
		{1040, KEY_START, true},  {1042, KEY_START, false},
		{1100, KEY_START, true},  {1102, KEY_START, false},
		{1310, KEY_QUIT, true},   {1312, KEY_QUIT, false},
		{1320, KEY_QUIT, true},   {1322, KEY_QUIT, false},
		{1360, KEY_START, true},  {1362, KEY_START, false},
		{1420, KEY_START, true},  {1422, KEY_START, false},
		{1665, KEY_START, true},  {1667, KEY_START, false},
	};
	// clang-format on

	const Tap kTaps[] = {{1000, KEY_RIGHT}, {1012, KEY_RIGHT}, {1024, KEY_RIGHT}};

	const Shot kShots[] = {
		{70, L"shots/01_title.png", Mode::Title},
		{140, L"shots/02_starmap.png", Mode::Grid},
		{215, L"shots/03_briefing.png", Mode::Briefing},
		{300, L"shots/04_descent.png", Mode::Playing},
		{430, L"shots/05_shaft.png", Mode::Playing},
		{560, L"shots/06_props.png", Mode::Playing},
		{680, L"shots/07_deep.png", Mode::Playing},
		{760, L"shots/08_tractor.png", Mode::Playing},
		{830, L"shots/09_field.png", Mode::Playing},
		{880, L"shots/10_help.png", Mode::Playing},
		{990, L"shots/11_starmap2.png", Mode::Grid},
		{1090, L"shots/12_briefing2.png", Mode::Briefing, kDeepPlanet},
		{1280, L"shots/13_hazards.png", Mode::Playing, kDeepPlanet},
		{1640, L"shots/14_results.png", Mode::Results, kDeepPlanet},
		{1695, L"shots/15_starmap3.png", Mode::Grid, kDeepPlanet},
	};

	bool HasParam(const std::string_view command_line, const std::string_view value)
	{
		for (const auto param : core::split(command_line, true))
			if (core::icmp(param, value) == 0) return true;
		return false;
	}

	class Application final : public ui::shell
	{
	public:
		Application() : shell(Options()), input_(keys()) {}

		static ui::shell_options Options()
		{
			ui::shell_options options;
			options.title = "zpilot";
			options.client_size = {1600, 900};
			options.minimum_client = {320, 240};
			options.gpu = true;
			options.steps_per_second = kStepsPerSecond;
			// Black behind the canvas, so a resize never flashes the default grey.
			options.colors = render::color_style{};
			return options;
		}

		bool init(const std::string_view command_line) override
		{
			hiddenCapture_ = HasParam(command_line, "--capture-headless") ||
			                 HasParam(command_line, "-capture-headless");
			capture_ = hiddenCapture_ || HasParam(command_line, "-capture");
			// A capture never moves the player's window placement, and the headless
			// one paints a real window that never appears or takes the keyboard.
			initial_options().remember_placement = !capture_;
			initial_options().offscreen = hiddenCapture_;
			if (capture_)
			{
				if (!PrepareCaptureProfile())
				{
					quit(1);
					return false;
				}
			}
			else
			{
				Profile::UseFolder({});
			}
			if (!shell::init(command_line))
			{
				Fail("platform window creation failed");
				quit(1);
				return false;
			}
			return true;
		}

		ui::element_ptr make_content() override
		{
			return std::make_shared<ui::canvas_element>([this](render::draw_context& dc, const core::recti bounds)
			{
				if (!ready_)
				{
					dc.draw_rect(bounds, core::color(0, 0, 0, 1));
					return;
				}
				const double start = platform::monotonic_seconds();
				game_.Draw(dc, bounds);
				if (shooting_) return;
				const double ms = (platform::monotonic_seconds() - start) * 1000.0;
				drawMs_ += ms;
				drawPeakMs_ = std::max(drawPeakMs_, ms);
				++draws_;
			});
		}

		bool start(std::string_view) override
		{
			if (hiddenCapture_ && !OffscreenVerified())
			{
				Fail("headless capture window appeared or took focus");
				quit(1);
				return false;
			}
			if (hiddenCapture_) synth_.SetMuted(true);
			if (!synth_.Init()) core::trace("zpilot: audio unavailable, continuing silently");
			if (!game_.Init(&synth_, &input_))
			{
				Fail("game initialisation failed");
				quit(1);
				return false;
			}
			SyncView();
			ready_ = true;
			return true;
		}

		void step(const double seconds) override { Advance(seconds); }

		bool key(const char32_t key, const platform::key_state keys) override
		{
			// Alt combinations belong to the native caption and system-menu path.
			return !keys.alt && Input::KeyFor(key) != KEY_COUNT;
		}

		// The pointer's buttons are keys like any other: fire and tractor.
		void on_mouse_left_button_down(const core::pointi loc, const platform::key_state keys) override
		{
			shell::on_mouse_left_button_down(loc, keys);
			this->keys().down(kPointerFire);
		}
		void on_mouse_left_button_double_click(const core::pointi loc, const platform::key_state keys) override
		{
			shell::on_mouse_left_button_double_click(loc, keys);
			this->keys().down(kPointerFire);
		}
		void on_mouse_left_button_up(const core::pointi loc, const platform::key_state keys) override
		{
			shell::on_mouse_left_button_up(loc, keys);
			this->keys().up(kPointerFire);
		}
		void on_mouse_right_button_down(const core::pointi loc, const platform::key_state keys) override
		{
			shell::on_mouse_right_button_down(loc, keys);
			this->keys().down(kPointerTractor);
		}
		void on_mouse_right_button_up(const core::pointi loc, const platform::key_state keys) override
		{
			shell::on_mouse_right_button_up(loc, keys);
			this->keys().up(kPointerTractor);
		}

		// Steps stand still while minimized, so the engine and the beam must not
		// go on droning at their last level.
		void minimized() override
		{
			synth_.SetThrust(0.0f);
			synth_.SetTractor(0.0f, 0.0f);
		}

		int main_loop() override
		{
			if (!capture_)
			{
				const int result = shell::main_loop();
				if (synth_.AudioFailed()) core::trace("zpilot: audio stream failed, continuing silently");
				return result;
			}
			return RunCapture();
		}

		void stop() override
		{
			ready_ = false;
			synth_.Shutdown();
			Profile::UseFolder({});
		}

	private:
		void Fail(std::string message)
		{
			if (startupError_.empty()) startupError_ = std::move(message);
			core::trace("zpilot: " + startupError_);
			if (capture_) platform::write_stdout("FAIL zpilot: " + startupError_ + "\n");
		}

		HWND NativeWindow() const
		{
			try
			{
				return std::any_cast<HWND>(frame()->handle());
			}
			catch (const std::bad_any_cast&)
			{
				return nullptr;
			}
		}

		// The offscreen window is shown so that it paints, but cloaked from the
		// desktop by the application, and never focused or brought to the front.
		bool OffscreenVerified() const
		{
			const HWND native = NativeWindow();
			DWORD cloaked = 0;
			return native && !frame()->has_focus() && GetForegroundWindow() != native &&
			       SUCCEEDED(DwmGetWindowAttribute(native, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
			       (cloaked & DWM_CLOAKED_APP) != 0;
		}

		// The camera frames the shots' shape in a capture and the window's in play.
		void SyncView()
		{
			const core::recti bounds = content_bounds();
			if (capture_ || bounds.is_empty()) game_.SetView(kShotExtent.cx, kShotExtent.cy);
			else game_.SetView(bounds.width(), bounds.height());
		}

		void Advance(const double seconds)
		{
			SyncView();
			game_.Step(static_cast<float>(seconds));
			if (game_.TakeFullscreenRequest())
			{
				fullscreen_ = !fullscreen_;
				frame()->full_screen(fullscreen_);
			}
			if (game_.TakeScreenshotRequest())
			{
				const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
									   std::chrono::system_clock::now().time_since_epoch())
				                       .count();
				const core::recti bounds = content_bounds();
				game_.ScreenshotSaved(SaveFrame(std::filesystem::path("zpilot_" + std::to_string(stamp) + ".png"),
				                                {bounds.width(), bounds.height()}));
			}
			if (game_.WantsQuit()) quit(0);
		}

		// The frame as the last step left it, drawn again by the software renderer
		// into a picture of its own.
		bool SaveFrame(const std::filesystem::path& path, const core::sizei extent)
		{
			if (extent.cx < 1 || extent.cy < 1) return false;
			shooting_ = true;
			const auto picture = ui::render_to_surface(content(), extent, core::color(0, 0, 0, 1));
			shooting_ = false;
			// Drawing it laid the canvas out at the picture's size; the window's
			// own layout comes back before it paints again.
			frame()->layout();
			return picture && SavePng(*picture, path);
		}

		void SetKey(const Key key, const bool down)
		{
			if (down) keys().down(Input::Code(key));
			else keys().up(Input::Code(key));
		}

		// The capture must exercise the same native message path as a player's tap.
		bool QueueTap(const Key key)
		{
			const HWND native = NativeWindow();
			const char32_t code = Input::Code(key);
			if (!native || code == 0) return false;
			constexpr LPARAM key_down_data = 1;
			constexpr LPARAM key_up_data = key_down_data | (static_cast<LPARAM>(1) << 30) |
			                               (static_cast<LPARAM>(1) << 31);
			return PostMessageW(native, WM_KEYDOWN, static_cast<WPARAM>(code), key_down_data) != FALSE &&
			       PostMessageW(native, WM_KEYUP, static_cast<WPARAM>(code), key_up_data) != FALSE;
		}

		bool PrepareCaptureProfile()
		{
			// create_folder takes a full path, so the capture's folder is named from where it runs.
			const auto shots = platform::current_directory().combine("shots");
			const auto created = platform::create_folder(shots);
			if (created.failed() && !platform::is_directory(shots))
			{
				Fail("capture directory creation failed: " + created.error_message);
				return false;
			}

			const core::file_path runProfile = shots.combine_file("profile.dat");
			const auto profileState = platform::file_attributes(runProfile);
			if (profileState.presence == platform::file_presence::unknown)
			{
				Fail("capture profile query failed");
				return false;
			}
			if (profileState.exists() && platform::is_directory(runProfile))
			{
				Fail("capture profile path is a directory");
				return false;
			}
			if (profileState.exists())
			{
				if (hiddenCapture_)
				{
					Fail("headless capture requires a fresh working directory");
					return false;
				}
				const auto removed = platform::delete_file(runProfile);
				if (removed.failed())
				{
					Fail("capture profile cleanup failed: " + removed.error_message);
					return false;
				}
			}
			Profile::UseFolder(shots);
			return true;
		}

		// Steps one frame at a time rather than by the clock, so the script lands
		// on the same frames however long the drawing takes.
		int RunCapture()
		{
			synth_.SetMuted(true);
			int wrongShot = -1;
			int rendered = 0;
			int shots = 0;
			double shotMs = 0.0;
			if (hiddenCapture_) platform::write_stdout("capture: hidden window verified\n");
			for (int frame = 0; frame <= 1700; ++frame)
			{
				if (!platform::pump_events()) break;
				if (hiddenCapture_ && !OffscreenVerified())
				{
					Fail("headless capture window appeared or took focus");
					return 1;
				}
				bool queuedTap = false;
				for (const Tap& tap : kTaps)
				{
					if (tap.frame != frame) continue;
					if (!QueueTap(tap.key))
					{
						Fail("native capture tap could not be queued");
						return 1;
					}
					queuedTap = true;
				}
				if (queuedTap && !platform::pump_events()) return 1;
				for (const Beat& beat : kScript)
					if (beat.frame == frame) SetKey(beat.key, beat.down);

				if (frame > 235 && frame < 915)
				{
					SetKey(KEY_UP, (frame % 46) < 13);
					SetKey(KEY_DOWN, (frame % 46) > 30 && (frame % 46) < 36);
					SetKey(KEY_FIRE, (frame % 150) < 8);
					const int c = frame % 260;
					SetKey(KEY_LEFT, c > 200 && c < 213);
					SetKey(KEY_RIGHT, c > 90 && c < 103);
				}
				if (frame > 1110 && frame < 1300)
				{
					SetKey(KEY_UP, (frame % 46) < 15);
					SetKey(KEY_DOWN, (frame % 46) > 32 && (frame % 46) < 38);
					SetKey(KEY_FIRE, (frame % 110) < 8);
					const int c = frame % 300;
					SetKey(KEY_LEFT, c > 230 && c < 244);
					SetKey(KEY_RIGHT, c > 100 && c < 114);
				}
				if (frame >= 1300 && frame < 1430)
				{
					SetKey(KEY_UP, false);
					SetKey(KEY_DOWN, false);
					SetKey(KEY_FIRE, false);
					SetKey(KEY_LEFT, false);
					SetKey(KEY_RIGHT, false);
				}
				if (frame >= 1430) SetKey(KEY_UP, true);

				Advance(1.0 / kStepsPerSecond);
				keys().next_step();
				++rendered;
				for (int i = 0; i < (int)std::size(kShots); ++i)
				{
					const Shot& shot = kShots[i];
					if (shot.frame != frame) continue;
					const double start = platform::monotonic_seconds();
					if (!SaveFrame(std::filesystem::path(shot.name), kShotExtent))
					{
						Fail(std::format("screenshot publication failed at frame {}", frame));
						return 1;
					}
					shotMs += (platform::monotonic_seconds() - start) * 1000.0;
					const bool wrong = game_.CurrentMode() != shot.mode ||
					                   (shot.planet >= 0 && game_.CurrentPlanet() != shot.planet);
					++shots;
					platform::write_stdout(std::format("capture: frame {} mode {} planet {} {}\n", frame,
					                                   static_cast<int>(game_.CurrentMode()), game_.CurrentPlanet(),
					                                   wrong ? "FAIL" : "PASS"));
					if (wrongShot < 0 && wrong) wrongShot = i;
				}
				present();
			}
			if (synth_.AudioFailed()) core::trace("zpilot: audio stream failed, continuing silently");
			platform::write_stdout(std::format("capture: {} frames rendered, {} screenshots, states {}\n", rendered,
			                                   shots, wrongShot < 0 ? "PASS" : "FAIL"));
			// What a frame costs to draw on the window's renderer, and a shot on the
			// software one, encoding included.
			platform::write_stdout(
				std::format("capture: {} window frames drawn, mean {:.2f} ms, peak {:.2f} ms; shots {:.0f} ms each\n",
			                draws_, draws_ ? drawMs_ / draws_ : 0.0, drawPeakMs_, shots ? shotMs / shots : 0.0));
			if (rendered != 1701 || shots != std::size(kShots)) return 1;
			return wrongShot < 0 ? 0 : kWrongShotExit + wrongShot;
		}

		Synth synth_;
		Input input_;
		Game game_;
		bool capture_ = false;
		bool hiddenCapture_ = false;
		bool ready_ = false;
		bool shooting_ = false; // the canvas is drawing a shot, not the window
		bool fullscreen_ = false;
		std::string startupError_;
		double drawMs_ = 0.0;
		double drawPeakMs_ = 0.0;
		int draws_ = 0;
	};

} // namespace

int main(int argc, char** argv)
{
	core::set_app_name("zpilot");
	return platform::run(std::make_shared<Application>(), argc, argv);
}

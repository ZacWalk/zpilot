// zpilot - entry point and deterministic capture driver.
#include "Game.h"

#include <filesystem>
#include <memory>
#include <string_view>

#include <libdesktop/platform.h>

using namespace zp;

namespace {

struct Beat {
    int frame;
    Key key;
    bool down;
};

// A key edge pair is sent through the same platform callback seam as a real keyboard event.
struct Tap {
    int frame;
    Key key;
};

struct Shot {
    int frame;
    const wchar_t* name;
    Mode mode;
    int planet = -1;  // any
};

constexpr int kWrongShotExit = 100;
constexpr int kDeepPlanet = 3;

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

bool HasParam(const std::string_view command_line, const std::string_view value) {
    for (const auto param : core::split(command_line, true))
        if (core::icmp(param, value) == 0) return true;
    return false;
}

class Application final : public platform::app {
public:
    bool parse_command_line(const std::string_view command_line) override {
        capture_ = HasParam(command_line, "-capture");
        return false;  // capture deliberately remains a visible, deterministic game run
    }

    bool init(const std::string_view command_line) override {
        capture_ = HasParam(command_line, "-capture");
        Initialise();
        return true;  // main_loop reports startup failures with the game's exit code
    }

    bool owns_loop() const override { return true; }

    int main_loop() override {
        if (!startupError_.empty()) return 1;
        return capture_ ? RunCapture() : RunInteractive();
    }

    bool can_exit() override { return true; }

    void exit() override {
        synth_.Shutdown();
        renderer_.Shutdown();
        window_.Destroy();
        Profile::UseFolder({});
    }

private:
    void Fail(std::string message) {
        if (startupError_.empty()) startupError_ = std::move(message);
        core::trace("zpilot: " + startupError_);
    }

    bool PrepareCaptureProfile() {
        // create_folder takes a full path, so the capture's folder is named from where it runs.
        const auto shots = platform::current_directory().combine("shots");
        const auto created = platform::create_folder(shots);
        if (created.failed() && !platform::is_directory(shots)) {
            Fail("capture directory creation failed: " + created.error_message);
            return false;
        }

        const core::file_path runProfile = shots.combine_file("profile.dat");
        const auto profileState = platform::file_attributes(runProfile);
        if (profileState.presence == platform::file_presence::unknown) {
            Fail("capture profile query failed");
            return false;
        }
        if (profileState.exists() && platform::is_directory(runProfile)) {
            Fail("capture profile path is a directory");
            return false;
        }
        if (profileState.exists()) {
            const auto removed = platform::delete_file(runProfile);
            if (removed.failed()) {
                Fail("capture profile cleanup failed: " + removed.error_message);
                return false;
            }
        }
        Profile::UseFolder(shots);
        return true;
    }

    void Initialise() {
        if (capture_) {
            if (!PrepareCaptureProfile()) return;
        } else {
            Profile::UseFolder({});
        }

        if (!window_.Create()) {
            Fail("platform window creation failed");
            return;
        }
        if (!renderer_.Init(window_.NativeWindow(), window_.Width(), window_.Height())) {
            const std::string error = renderer_.Error().empty()
                                          ? "Direct3D 11 initialisation failed"
                                          : renderer_.Error();
            if (capture_)
                core::trace("zpilot: " + error);
            else
                platform::show_error(error);
            Fail(error);
            return;
        }
        if (!synth_.Init())
            core::trace("zpilot: audio unavailable, continuing silently");
        if (!game_.Init(&renderer_, &synth_, &window_)) {
            Fail("game initialisation failed");
            return;
        }
    }

    int RunCapture() {
        synth_.SetMuted(true);
        int wrongShot = -1;
        for (int frame = 0; frame <= 1700; ++frame) {
            window_.NewFrame();
            if (!window_.PumpMessages()) break;
            bool queuedTap = false;
            for (const Tap& tap : kTaps) {
                if (tap.frame != frame) continue;
                if (!window_.QueueTap(tap.key)) return 1;
                queuedTap = true;
            }
            if (queuedTap && !window_.PumpMessages()) return 1;
            for (const Beat& beat : kScript)
                if (beat.frame == frame) window_.SetKey(beat.key, beat.down);

            if (frame > 235 && frame < 915) {
                window_.SetKey(KEY_UP, (frame % 46) < 13);
                window_.SetKey(KEY_DOWN, (frame % 46) > 30 && (frame % 46) < 36);
                window_.SetKey(KEY_FIRE, (frame % 150) < 8);
                const int c = frame % 260;
                window_.SetKey(KEY_LEFT, c > 200 && c < 213);
                window_.SetKey(KEY_RIGHT, c > 90 && c < 103);
            }
            if (frame > 1110 && frame < 1300) {
                window_.SetKey(KEY_UP, (frame % 46) < 15);
                window_.SetKey(KEY_DOWN, (frame % 46) > 32 && (frame % 46) < 38);
                window_.SetKey(KEY_FIRE, (frame % 110) < 8);
                const int c = frame % 300;
                window_.SetKey(KEY_LEFT, c > 230 && c < 244);
                window_.SetKey(KEY_RIGHT, c > 100 && c < 114);
            }
            if (frame >= 1300 && frame < 1430) {
                window_.SetKey(KEY_UP, false);
                window_.SetKey(KEY_DOWN, false);
                window_.SetKey(KEY_FIRE, false);
                window_.SetKey(KEY_LEFT, false);
                window_.SetKey(KEY_RIGHT, false);
            }
            if (frame >= 1430) window_.SetKey(KEY_UP, true);

            game_.Frame(1.0f / 60.0f);
            for (int i = 0; i < (int)std::size(kShots); ++i) {
                const Shot& shot = kShots[i];
                if (shot.frame != frame) continue;
                if (!renderer_.SaveScreenshot(std::filesystem::path(shot.name))) return 1;
                const bool wrong = game_.CurrentMode() != shot.mode ||
                                   (shot.planet >= 0 && game_.CurrentPlanet() != shot.planet);
                if (wrongShot < 0 && wrong) wrongShot = i;
            }
            renderer_.Present(false);
        }
        if (synth_.AudioFailed())
            core::trace("zpilot: audio stream failed, continuing silently");
        return wrongShot < 0 ? 0 : kWrongShotExit + wrongShot;
    }

    int RunInteractive() {
        double previous = platform::monotonic_seconds();
        while (true) {
            window_.NewFrame();
            if (!window_.PumpMessages()) break;
            if (window_.SizeChanged()) renderer_.Resize(window_.Width(), window_.Height());

            const double now = platform::monotonic_seconds();
            const float dt = static_cast<float>(now - previous);
            previous = now;
            game_.Frame(dt);
            renderer_.Present(true);
            if (game_.WantsQuit()) break;
        }
        if (synth_.AudioFailed())
            core::trace("zpilot: audio stream failed, continuing silently");
        return 0;
    }

    Window window_;
    Renderer renderer_;
    Synth synth_;
    Game game_;
    bool capture_ = false;
    std::string startupError_;
};

}  // namespace

int main(int argc, char** argv) {
    core::set_app_name("zpilot");
    auto app = std::make_shared<Application>();
    return platform::run(app, argc, argv);
}

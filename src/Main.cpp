// zpilot - entry point and deterministic capture driver.
#include "Game.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>

#include "platform.h"

using namespace zp;

namespace {

struct Beat {
    int frame;
    Key key;
    bool down;
};

// A key edge pair: queued through libdesktop after the frame pump, then flushed
// through the same reactor path as a real keyboard event.
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

bool HasParam(const std::span<const std::string_view> params, const std::string_view value) {
    for (const auto param : params)
        if (pf::icmp(param, value) == 0) return true;
    return false;
}

class Application {
public:
    bool Initialise(const pf::window_frame_ptr& frame, bool capture) {
        capture_ = capture;
        if (capture_) {
            const pf::file_path shots("shots");
            if (!pf::platform_create_directory(shots)) {
                pf::debug_trace("zpilot: capture directory creation failed: " +
                                pf::platform_last_error_message());
                return false;
            }
            const pf::file_path runProfile = shots.combine("profile.dat");
            const auto profileState = pf::query_file(runProfile);
            if (profileState.status == pf::file_query_status::failed) {
                pf::debug_trace("zpilot: capture profile query failed: " +
                                profileState.error.message);
                return false;
            }
            if (profileState.found() && pf::is_directory(runProfile)) {
                pf::debug_trace("zpilot: capture profile path is a directory");
                return false;
            }
            if (profileState.found() && !pf::platform_delete_file(runProfile)) {
                pf::debug_trace("zpilot: capture profile cleanup failed: " +
                                pf::platform_last_error_message());
                return false;
            }
            Profile::UseFolder(shots);
        } else {
            Profile::UseFolder({});
        }
        return window_.Attach(frame);
    }

    int Run() {
        if (!renderer_.Init(window_.Frame(), window_.Width(), window_.Height())) {
            const std::string error = renderer_.Error().empty()
                                          ? "Direct3D 11 initialisation failed"
                                          : renderer_.Error();
            if (capture_)
                pf::debug_trace("zpilot: " + error);
            else
                pf::platform_show_error(error, "zpilot");
            return 1;
        }
        if (!synth_.Init())
            pf::debug_trace("zpilot: audio unavailable, continuing silently");
        if (!game_.Init(&renderer_, &synth_, &window_)) return 1;

        const int result = capture_ ? RunCapture() : RunInteractive();
        if (synth_.AudioFailed())
            pf::debug_trace("zpilot: audio stream failed, continuing silently");
        return result;
    }

    void Shutdown() {
        synth_.Shutdown();
        renderer_.Shutdown();
        window_.Destroy();
        Profile::UseFolder({});
    }

private:
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
        return wrongShot < 0 ? 0 : kWrongShotExit + wrongShot;
    }

    int RunInteractive() {
        double previous = pf::platform_get_time();
        while (true) {
            window_.NewFrame();
            if (!window_.PumpMessages()) break;
            if (window_.SizeChanged()) renderer_.Resize(window_.Width(), window_.Height());

            const double now = pf::platform_get_time();
            const float dt = static_cast<float>(now - previous);
            previous = now;
            game_.Frame(dt);
            renderer_.Present(true);
            if (game_.WantsQuit()) break;
        }
        return 0;
    }

    Window window_;
    Renderer renderer_;
    Synth synth_;
    Game game_;
    bool capture_ = false;
};

std::unique_ptr<Application> g_application;

}  // namespace

app_init_result app_init(const pf::window_frame_ptr& main_frame,
                         const std::span<const std::string_view> params) {
    g_application = std::make_unique<Application>();
    const bool capture = HasParam(params, "-capture");
    if (!g_application->Initialise(main_frame, capture))
        return {false, 1};

    app_init_result result;
    result.window.initial_client_size = {1600, 900};
    result.window.minimum_client_size = {320, 240};
    result.window.fullscreen = false;
    result.window.resizable = true;
    result.main_loop = [] {
        return g_application ? g_application->Run() : 1;
    };
    return result;
}

void app_idle() {}

void app_destroy() {
    if (g_application) {
        g_application->Shutdown();
        g_application.reset();
    }
}

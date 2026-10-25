// zpilot - entry point.
#include "Game.h"

using namespace zp;

// Deterministic fly-through used to render reference frames without a human at
// the keyboard: zpilot.exe -capture
namespace {

struct Beat {
    int frame;
    Key key;
    bool down;
};
// A key typed for real: sent down and up straight to the window procedure after
// the frame's pump, so both land between two frames - the quick tap a held
// SetKey never exercises - and no focus change can slip in between them.
struct Tap {
    int frame;
    WPARAM vk;
};
// A lit frame is not proof the script did what the name says - a swallowed key
// leaves the game on the screen before - so every shot also names its mode, and
// the later ones the planet the taps should have chosen.
struct Shot {
    int frame;
    const wchar_t* name;
    Mode mode;
    int planet = -1;  // any
};

// The capture's exit code is this plus the index of the first shot that caught
// a different mode or planet; tests/capture.ps1 turns it back into the shot's name.
constexpr int kWrongShotExit = 100;

constexpr int kDeepPlanet = 3;  // three taps right of the first

const Beat kScript[] = {
    {80, KEY_START, true},    {82, KEY_START, false},    // title -> star map
    {150, KEY_START, true},   {152, KEY_START, false},   // launch the first planet
    {225, KEY_START, true},   {227, KEY_START, false},   // skip the briefing
    {700, KEY_TRACTOR, true}, {770, KEY_TRACTOR, false},
    {800, KEY_SHIELD, true},  {840, KEY_SHIELD, false},  // overcharge the field
    {860, KEY_HELP, true},    {862, KEY_HELP, false},
    {900, KEY_HELP, true},    {902, KEY_HELP, false},
    // Second act: back out to the star map and fly something deeper (kTaps).
    {920, KEY_QUIT, true},    {922, KEY_QUIT, false},    // pause
    {930, KEY_QUIT, true},    {932, KEY_QUIT, false},    // -> star map
    {1040, KEY_START, true},  {1042, KEY_START, false},  // launch it
    {1100, KEY_START, true},  {1102, KEY_START, false},  // skip the briefing
    // Third act: back to the star map, relaunch, and climb straight out. That
    // abandons the planet, which is the only scripted route to the results
    // screen and the profile write sitting behind it.
    {1310, KEY_QUIT, true},   {1312, KEY_QUIT, false},   // pause
    {1320, KEY_QUIT, true},   {1322, KEY_QUIT, false},   // -> star map
    {1360, KEY_START, true},  {1362, KEY_START, false},  // relaunch
    {1420, KEY_START, true},  {1422, KEY_START, false},  // skip the briefing
    // The ship clears the exit line near frame 1617, and the results screen
    // ignores keys for its first half second, so this is the earliest press
    // that lands with room to spare.
    {1665, KEY_START, true},  {1667, KEY_START, false},  // results -> star map
};

const Tap kTaps[] = {{1000, VK_RIGHT}, {1012, VK_RIGHT}, {1024, VK_RIGHT}};

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

bool HasFlag(const wchar_t* flag) {
    const wchar_t* cmd = GetCommandLineW();
    return cmd && wcsstr(cmd, flag) != nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // The capture's runs go in a save file of their own, started empty, so they
    // never reach the player's log and the star map frames never depend on it.
    const bool capture = HasFlag(L"-capture");
    if (capture) {
        CreateDirectoryW(L"shots", nullptr);
        DeleteFileW(L"shots\\profile.dat");
        Profile::UseFolder(L"shots");
    }

    Window window;
    if (!window.Create(L"zpilot", 1600, 900)) return 1;

    Renderer renderer;
    if (!renderer.Init(window.Handle(), window.Width(), window.Height())) {
        MessageBoxW(nullptr, L"Direct3D 11 initialisation failed.", L"zpilot", MB_OK | MB_ICONERROR);
        return 1;
    }

    Synth synth;
    if (!synth.Init()) {
        OutputDebugStringA("zpilot: audio unavailable, continuing silently\n");
    }

    Game game;
    game.Init(&renderer, &synth, &window);

    if (capture) {
        synth.SetMuted(true);
        int wrongShot = -1;
        for (int frame = 0; frame <= 1700; ++frame) {
            window.NewFrame();
            window.PumpMessages();
            for (const Tap& t : kTaps) {
                if (t.frame != frame) continue;
                SendMessageW(window.Handle(), WM_KEYDOWN, t.vk, 1);
                SendMessageW(window.Handle(), WM_KEYUP, t.vk, 0xC0000001);
            }
            for (const Beat& b : kScript)
                if (b.frame == frame) window.SetKey(b.key, b.down);
            if (frame > 235 && frame < 915) {
                // A crude autopilot: pulse the drive to ride the shaft down, and
                // dab the retro so the capture exercises both directions.
                window.SetKey(KEY_UP, (frame % 46) < 13);
                window.SetKey(KEY_DOWN, (frame % 46) > 30 && (frame % 46) < 36);
                window.SetKey(KEY_FIRE, (frame % 150) < 8);
                int c = frame % 260;
                window.SetKey(KEY_LEFT, c > 200 && c < 213);
                window.SetKey(KEY_RIGHT, c > 90 && c < 103);
            }
            if (frame > 1110 && frame < 1300) {
                window.SetKey(KEY_UP, (frame % 46) < 15);
                window.SetKey(KEY_DOWN, (frame % 46) > 32 && (frame % 46) < 38);
                window.SetKey(KEY_FIRE, (frame % 110) < 8);
                int c = frame % 300;
                window.SetKey(KEY_LEFT, c > 230 && c < 244);
                window.SetKey(KEY_RIGHT, c > 100 && c < 114);
            }
            if (frame >= 1300 && frame < 1430) {
                window.SetKey(KEY_UP, false);
                window.SetKey(KEY_DOWN, false);
                window.SetKey(KEY_FIRE, false);
                window.SetKey(KEY_LEFT, false);
                window.SetKey(KEY_RIGHT, false);
            }
            // The spawn sits in open sky below the exit line, so holding the
            // drive is a reliable way to leave without the orb.
            if (frame >= 1430) window.SetKey(KEY_UP, true);
            game.Frame(1.0f / 60.0f);
            for (int i = 0; i < (int)_countof(kShots); ++i) {
                const Shot& s = kShots[i];
                if (s.frame != frame) continue;
                renderer.SaveScreenshot(s.name);
                const bool wrong = game.CurrentMode() != s.mode ||
                                   (s.planet >= 0 && game.CurrentPlanet() != s.planet);
                if (wrongShot < 0 && wrong) wrongShot = i;
            }
            renderer.Present(false);
        }
        synth.Shutdown();
        renderer.Shutdown();
        window.Destroy();
        CoUninitialize();
        return wrongShot < 0 ? 0 : kWrongShotExit + wrongShot;
    }

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (true) {
        window.NewFrame();
        if (!window.PumpMessages()) break;
        if (window.SizeChanged()) renderer.Resize(window.Width(), window.Height());

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;

        game.Frame(dt);
        renderer.Present(true);

        if (game.WantsQuit()) break;
    }

    synth.Shutdown();
    renderer.Shutdown();
    window.Destroy();
    CoUninitialize();
    return 0;
}

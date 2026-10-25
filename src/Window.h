// zpilot - Win32 window + keyboard/mouse input state.
#pragma once
#include "Common.h"

namespace zp {

enum Key {
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_FIRE, KEY_TRACTOR, KEY_SHIELD, KEY_SLOW,
    KEY_START, KEY_PAUSE, KEY_RESTART, KEY_QUIT,
    KEY_HELP, KEY_MUTE, KEY_FULLSCREEN, KEY_SHOT,
    KEY_COUNT
};

class Window {
public:
    bool Create(const wchar_t* title, int w, int h);
    void Destroy();
    bool PumpMessages();  // false when the app should quit

    HWND Handle() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool SizeChanged();  // consumes the flag

    bool Down(Key k) const { return down_[k]; }
    // A tap that goes down and up between two frames still counts as a press.
    bool Pressed(Key k) const { return (down_[k] && !prevDown_[k]) || tapped_[k]; }
    bool Released(Key k) const { return !down_[k] && prevDown_[k]; }
    void SetKey(Key k, bool down) { down_[k] = down; }  // used by the scripted capture mode
    void NewFrame();  // latch previous key state

    void ToggleFullscreen();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void SetVirtualKey(WPARAM vk, bool down, bool fresh);
    void SetButton(Key k, bool down);

    HWND hwnd_ = nullptr;
    int width_ = 1600, height_ = 900;
    bool sizeChanged_ = false;
    bool quit_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT placement_ = {sizeof(WINDOWPLACEMENT)};
    bool down_[KEY_COUNT] = {};
    bool prevDown_[KEY_COUNT] = {};
    bool tapped_[KEY_COUNT] = {};  // went down since the last NewFrame
};

}  // namespace zp

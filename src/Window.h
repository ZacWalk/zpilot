// zpilot - shared-platform window + keyboard/mouse input state.
#pragma once

#include <cstdint>
#include <memory>

#include "Common.h"
#include <libdesktop/platform.h>

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
    Window();
    ~Window();

    bool Create();
    bool Attach(const platform::window_ptr& window);  // test seam for a fake platform window
    void Destroy();
    bool PumpMessages();  // false when the app should quit

    const platform::window_ptr& NativeWindow() const { return window_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool SizeChanged();  // consumes the flag

    bool Down(Key k) const { return down_[k]; }
    // A tap that goes down and up between two frames still counts as a press.
    bool Pressed(Key k) const { return (down_[k] && !prevDown_[k]) || tapped_[k]; }
    bool Released(Key k) const { return !down_[k] && prevDown_[k]; }
    void SetKey(Key k, bool down) { down_[k] = down; }  // used by the scripted capture mode
    bool QueueTap(Key k);  // the capture must exercise the native message path
    bool InjectKeyCode(uint32_t vk, bool down, bool repeat = false);  // headless input seam
    void NewFrame();  // latch previous key state

    void ToggleFullscreen();

    // The test suite drives the same callback object that a native platform window uses.
    std::shared_ptr<platform::window_events> EventSink() const;

private:
    struct Events;

    void SetVirtualKey(uint32_t vk, bool down, bool fresh);
    void SetButton(Key k, bool down);
    void ClearInput();

    platform::window_ptr window_;
    std::shared_ptr<Events> events_;
    int width_ = 1600, height_ = 900;
    bool sizeChanged_ = false;
    bool quit_ = false;
    bool down_[KEY_COUNT] = {};
    bool prevDown_[KEY_COUNT] = {};
    bool tapped_[KEY_COUNT] = {};  // went down since the last NewFrame
};

}  // namespace zp

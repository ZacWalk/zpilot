// zpilot - shared-platform window + keyboard/mouse input state.
#pragma once

#include <cstdint>
#include <memory>

#include "Common.h"
#include "platform.h"

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
    ~Window();
    bool Attach(const pf::window_frame_ptr& frame);
    void Destroy();
    bool PumpMessages();  // false when the app should quit

    const pf::window_frame_ptr& Frame() const { return frame_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool SizeChanged();  // consumes the flag

    bool Down(Key k) const { return down_[k]; }
    // A tap that goes down and up between two frames still counts as a press.
    bool Pressed(Key k) const { return (down_[k] && !prevDown_[k]) || tapped_[k]; }
    bool Released(Key k) const { return !down_[k] && prevDown_[k]; }
    void SetKey(Key k, bool down) { down_[k] = down; }  // used by the scripted capture mode
    bool QueueTap(Key k);  // queues both edges through the shared frame backend
    void InjectKeyCode(uint32_t vk, bool down, bool repeat = false);  // headless input seam
    void NewFrame();  // latch previous key state

    void ToggleFullscreen();

private:
    struct Reactor;

    void SetVirtualKey(uint32_t vk, bool down, bool fresh);
    void SetButton(Key k, bool down);
    void ClearInput();

    pf::window_frame_ptr frame_;
    pf::frame_reactor_ptr reactor_;
    int width_ = 1600, height_ = 900;
    bool sizeChanged_ = false;
    bool quit_ = false;
    bool down_[KEY_COUNT] = {};
    bool prevDown_[KEY_COUNT] = {};
    bool tapped_[KEY_COUNT] = {};  // went down since the last NewFrame
};

}  // namespace zp

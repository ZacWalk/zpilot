#include "Window.h"

#include <any>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace zp {

namespace {

Key KeyForVirtualCode(const uint32_t vk) {
    if (vk == static_cast<uint32_t>(platform::keys::LEFT) || vk == 'A') return KEY_LEFT;
    if (vk == static_cast<uint32_t>(platform::keys::RIGHT) || vk == 'D') return KEY_RIGHT;
    if (vk == static_cast<uint32_t>(platform::keys::UP) || vk == 'W') return KEY_UP;
    if (vk == static_cast<uint32_t>(platform::keys::DOWN) || vk == 'S') return KEY_DOWN;
    if (vk == static_cast<uint32_t>(platform::keys::SPACE)) return KEY_FIRE;
    if (vk == static_cast<uint32_t>(platform::keys::SHIFT) || vk == 'E') return KEY_TRACTOR;
    if (vk == static_cast<uint32_t>(platform::keys::CONTROL) || vk == 'Q') return KEY_SHIELD;
    if (vk == static_cast<uint32_t>(platform::keys::TAB) || vk == 'F') return KEY_SLOW;
    if (vk == static_cast<uint32_t>(platform::keys::RETURN)) return KEY_START;
    if (vk == 'P') return KEY_PAUSE;
    if (vk == 'R') return KEY_RESTART;
    if (vk == static_cast<uint32_t>(platform::keys::ESCAPE)) return KEY_QUIT;
    if (vk == static_cast<uint32_t>(platform::keys::F1)) return KEY_HELP;
    if (vk == static_cast<uint32_t>(platform::keys::F3)) return KEY_MUTE;
    if (vk == static_cast<uint32_t>(platform::keys::F11)) return KEY_FULLSCREEN;
    if (vk == static_cast<uint32_t>(platform::keys::F12)) return KEY_SHOT;
    return KEY_COUNT;
}

uint32_t VirtualCodeForKey(const Key key) {
    switch (key) {
        case KEY_LEFT: return static_cast<uint32_t>(platform::keys::LEFT);
        case KEY_RIGHT: return static_cast<uint32_t>(platform::keys::RIGHT);
        case KEY_UP: return static_cast<uint32_t>(platform::keys::UP);
        case KEY_DOWN: return static_cast<uint32_t>(platform::keys::DOWN);
        case KEY_FIRE: return static_cast<uint32_t>(platform::keys::SPACE);
        case KEY_TRACTOR: return static_cast<uint32_t>(platform::keys::SHIFT);
        case KEY_SHIELD: return static_cast<uint32_t>(platform::keys::CONTROL);
        case KEY_SLOW: return static_cast<uint32_t>(platform::keys::TAB);
        case KEY_START: return static_cast<uint32_t>(platform::keys::RETURN);
        case KEY_PAUSE: return 'P';
        case KEY_RESTART: return 'R';
        case KEY_QUIT: return static_cast<uint32_t>(platform::keys::ESCAPE);
        case KEY_HELP: return static_cast<uint32_t>(platform::keys::F1);
        case KEY_MUTE: return static_cast<uint32_t>(platform::keys::F3);
        case KEY_FULLSCREEN: return static_cast<uint32_t>(platform::keys::F11);
        case KEY_SHOT: return static_cast<uint32_t>(platform::keys::F12);
        default: return 0;
    }
}

}  // namespace

struct Window::Events final : platform::window_events {
    explicit Events(Window* owner_) : owner(owner_) {}

    void created(platform::window& window) override {
        const core::sizei extent = window.client_extent();
        if (extent.cx > 0 && extent.cy > 0) {
            owner->width_ = extent.cx;
            owner->height_ = extent.cy;
        }
    }

    void destroyed() override {
        owner->quit_ = true;
        platform::quit();
    }

    void size_changed(core::sizei extent, bool minimized) override {
        if (!minimized && extent.cx > 0 && extent.cy > 0) {
            owner->width_ = extent.cx;
            owner->height_ = extent.cy;
            owner->sizeChanged_ = true;
        }
    }

    void pointer_down(platform::pointer_button button, core::pointi, platform::key_state) override {
        if (button == platform::pointer_button::left) owner->SetButton(KEY_FIRE, true);
        if (button == platform::pointer_button::right) owner->SetButton(KEY_TRACTOR, true);
    }

    void pointer_up(platform::pointer_button button, core::pointi, platform::key_state) override {
        if (button == platform::pointer_button::left) owner->SetButton(KEY_FIRE, false);
        if (button == platform::pointer_button::right) owner->SetButton(KEY_TRACTOR, false);
    }

    bool key_down(int key, platform::key_state keys, bool system) override {
        const bool handled = owner->InjectKeyCode(static_cast<uint32_t>(key), true);
        // Alt combinations belong to the native caption and system-menu path, not gameplay.
        return handled && !system && !keys.alt;
    }

    bool key_up(int key, platform::key_state keys, bool system) override {
        const bool handled = owner->InjectKeyCode(static_cast<uint32_t>(key), false);
        // Let Windows finish the same system-key sequence it received on the way down.
        return handled && !system && !keys.alt;
    }

    void focus_changed(bool has_focus) override {
        if (!has_focus) owner->ClearInput();
    }

    void close_requested() override {
        owner->quit_ = true;
        if (owner->window_) owner->window_->destroy();
        platform::quit();
    }

    Window* owner = nullptr;
};

Window::Window() : events_(std::make_shared<Events>(this)) {}

Window::~Window() {
    Destroy();
}

bool Window::Create() {
    if (window_) Destroy();
    if (!events_) events_ = std::make_shared<Events>(this);

    platform::window_options options;
    options.kind = platform::window_kind::app;
    options.client_size = {1600, 900};
    options.minimum_client = {320, 240};
    options.visible = false;
    options.drawn_caption = false;
    options.resizable = true;
    options.title = "zpilot";

    const auto created = platform::create_window(options, events_);
    if (!created) return false;
    return Attach(created);
}

bool Window::Attach(const platform::window_ptr& window) {
    if (window_) Destroy();
    if (!events_) events_ = std::make_shared<Events>(this);
    window_ = window;
    quit_ = false;
    sizeChanged_ = false;
    if (!window_) return false;
    const core::sizei extent = window_->client_extent();
    if (extent.cx > 0) width_ = extent.cx;
    if (extent.cy > 0) height_ = extent.cy;
    return true;
}

void Window::Destroy() {
    if (window_) window_->destroy();
    window_.reset();
    events_.reset();
    ClearInput();
}

bool Window::PumpMessages() {
    if (!platform::pump_events()) quit_ = true;
    return !quit_;
}

bool Window::SizeChanged() {
    const bool changed = sizeChanged_;
    sizeChanged_ = false;
    return changed;
}

void Window::NewFrame() {
    std::memcpy(prevDown_, down_, sizeof(down_));
    std::memset(tapped_, 0, sizeof(tapped_));
}

bool Window::QueueTap(const Key key) {
    // Capture must exercise the same native message path as a player's tap.
    if (!window_) return false;
    const uint32_t vk = VirtualCodeForKey(key);
    if (vk == 0) return false;

    HWND native = nullptr;
    try {
        native = std::any_cast<HWND>(window_->native_handle());
    } catch (const std::bad_any_cast&) {
        return false;
    }
    if (!native) return false;

    constexpr LPARAM key_down_data = 1;
    constexpr LPARAM key_up_data = key_down_data | (static_cast<LPARAM>(1) << 30) |
                                   (static_cast<LPARAM>(1) << 31);
    return PostMessageW(native, WM_KEYDOWN, static_cast<WPARAM>(vk), key_down_data) != FALSE &&
           PostMessageW(native, WM_KEYUP, static_cast<WPARAM>(vk), key_up_data) != FALSE;
}

bool Window::InjectKeyCode(const uint32_t vk, const bool down, const bool repeat) {
    const Key k = KeyForVirtualCode(vk);
    if (k == KEY_COUNT) return false;
    const bool fresh = down && !repeat && !down_[k];
    SetVirtualKey(vk, down, fresh);
    return true;
}

void Window::ToggleFullscreen() {
    if (!window_) return;
    window_->full_screen(!window_->is_full_screen());
}

void Window::SetVirtualKey(const uint32_t vk, const bool down, const bool fresh) {
    const Key k = KeyForVirtualCode(vk);
    if (k == KEY_COUNT) return;
    down_[k] = down;
    if (down && fresh) tapped_[k] = true;
}

void Window::SetButton(const Key k, const bool down) {
    down_[k] = down;
    if (down) tapped_[k] = true;
}

void Window::ClearInput() {
    std::memset(down_, 0, sizeof(down_));
    std::memset(prevDown_, 0, sizeof(prevDown_));
    std::memset(tapped_, 0, sizeof(tapped_));
}

std::shared_ptr<platform::window_events> Window::EventSink() const {
    return events_;
}

}  // namespace zp

#include "Window.h"

#include <cstring>

namespace zp {

namespace {

Key KeyForVirtualCode(const uint32_t vk) {
    switch (vk) {
        case pf::platform_key::Left: case 'A': return KEY_LEFT;
        case pf::platform_key::Right: case 'D': return KEY_RIGHT;
        case pf::platform_key::Up: case 'W': return KEY_UP;
        case pf::platform_key::Down: case 'S': return KEY_DOWN;
        case pf::platform_key::Space: return KEY_FIRE;
        case pf::platform_key::Shift: case 'E': return KEY_TRACTOR;
        case pf::platform_key::Control: case 'Q': return KEY_SHIELD;
        case pf::platform_key::Tab: case 'F': return KEY_SLOW;
        case pf::platform_key::Return: return KEY_START;
        case 'P': return KEY_PAUSE;
        case 'R': return KEY_RESTART;
        case pf::platform_key::Escape: return KEY_QUIT;
        case pf::platform_key::F1: return KEY_HELP;
        case pf::platform_key::F3: return KEY_MUTE;
        case pf::platform_key::F11: return KEY_FULLSCREEN;
        case pf::platform_key::F12: return KEY_SHOT;
        default: return KEY_COUNT;
    }
}

uint32_t VirtualCodeForKey(const Key key) {
    switch (key) {
        case KEY_LEFT: return pf::platform_key::Left;
        case KEY_RIGHT: return pf::platform_key::Right;
        case KEY_UP: return pf::platform_key::Up;
        case KEY_DOWN: return pf::platform_key::Down;
        case KEY_FIRE: return pf::platform_key::Space;
        case KEY_TRACTOR: return pf::platform_key::Shift;
        case KEY_SHIELD: return pf::platform_key::Control;
        case KEY_SLOW: return pf::platform_key::Tab;
        case KEY_START: return pf::platform_key::Return;
        case KEY_PAUSE: return 'P';
        case KEY_RESTART: return 'R';
        case KEY_QUIT: return pf::platform_key::Escape;
        case KEY_HELP: return pf::platform_key::F1;
        case KEY_MUTE: return pf::platform_key::F3;
        case KEY_FULLSCREEN: return pf::platform_key::F11;
        case KEY_SHOT: return pf::platform_key::F12;
        default: return 0;
    }
}

}  // namespace

struct Window::Reactor final : pf::frame_reactor {
    explicit Reactor(Window* owner_) : owner(owner_) {}

    uint32_t handle_message(pf::window_frame_ptr frame, pf::message_type message,
                            const pf::message_params&) override {
        switch (message) {
            case pf::message_type::create:
                frame->set_text("zpilot");
                return 0;
            case pf::message_type::destroy:
                owner->quit_ = true;
                return 0;
            case pf::message_type::close:
                owner->quit_ = true;
                frame->close();
                return 0;
            case pf::message_type::kill_focus:
                owner->ClearInput();
                return 0;
            case pf::message_type::erase_background:
                // The D3D backend redraws the entire client area every frame.
                return 1;
            default:
                return 0;
        }
    }

    uint32_t handle_keyboard(pf::window_frame_ptr, pf::keyboard_message_type message,
                             const pf::keyboard_params& params) override {
        if (message == pf::keyboard_message_type::key_down)
            owner->InjectKeyCode(params.vk, true, params.repeat);
        else if (message == pf::keyboard_message_type::key_up)
            owner->InjectKeyCode(params.vk, false);
        return 0;
    }

    uint32_t handle_mouse(pf::window_frame_ptr, pf::mouse_message_type message,
                          const pf::mouse_params&) override {
        switch (message) {
            case pf::mouse_message_type::left_button_down: owner->SetButton(KEY_FIRE, true); break;
            case pf::mouse_message_type::left_button_up: owner->SetButton(KEY_FIRE, false); break;
            case pf::mouse_message_type::right_button_down: owner->SetButton(KEY_TRACTOR, true); break;
            case pf::mouse_message_type::right_button_up: owner->SetButton(KEY_TRACTOR, false); break;
            default: break;
        }
        return 0;
    }

    void handle_paint(pf::window_frame_ptr&, pf::draw_context&) override {}

    void handle_size(pf::window_frame_ptr&, pf::isize extent, pf::measure_context&) override {
        owner->width_ = extent.cx;
        owner->height_ = extent.cy;
        owner->sizeChanged_ = true;
    }

    Window* owner = nullptr;
};

Window::~Window() {
    Destroy();
}

bool Window::Attach(const pf::window_frame_ptr& frame) {
    if (frame_) Destroy();
    frame_ = frame;
    if (!frame_) return false;
    reactor_ = std::make_shared<Reactor>(this);
    frame_->set_reactor(reactor_);
    const pf::irect client = frame_->get_client_rect();
    if (client.width() > 0) width_ = client.width();
    if (client.height() > 0) height_ = client.height();
    return true;
}

void Window::Destroy() {
    if (frame_) frame_->set_reactor(nullptr);
    reactor_.reset();
    frame_.reset();
}

bool Window::PumpMessages() {
    if (!pf::platform_events()) quit_ = true;
    return !quit_;
}

bool Window::SizeChanged() {
    const bool changed = sizeChanged_;
    sizeChanged_ = false;
    return changed;
}

void Window::NewFrame() {
    memcpy(prevDown_, down_, sizeof(down_));
    memset(tapped_, 0, sizeof(tapped_));
}

bool Window::QueueTap(const Key key) {
    if (!frame_) return false;
    const uint32_t vk = VirtualCodeForKey(key);
    return vk != 0 && pf::post_key_events(frame_, vk, true) &&
           pf::post_key_events(frame_, vk, false);
}

void Window::InjectKeyCode(const uint32_t vk, const bool down, const bool repeat) {
    const Key k = KeyForVirtualCode(vk);
    if (k == KEY_COUNT) return;
    SetVirtualKey(vk, down, down && !repeat);
}

void Window::ToggleFullscreen() {
    if (!frame_) return;
    frame_->toggle_fullscreen();
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
    memset(down_, 0, sizeof(down_));
    memset(prevDown_, 0, sizeof(prevDown_));
    memset(tapped_, 0, sizeof(tapped_));
}

}  // namespace zp

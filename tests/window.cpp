#include "../src/Window.h"

#include <any>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class FakeWindow final : public platform::window {
public:
    std::any native_handle() const override { return {}; }
    platform::window_kind kind() const override { return platform::window_kind::app; }
    void destroy() override { destroyed = true; }

    core::recti bounds() const override { return {0, 0, 1600, 900}; }
    void set_bounds(core::recti value, bool visible) override {
        bounds_ = value;
        visible_ = visible;
    }
    core::sizei client_extent() const override { return {1600, 900}; }
    void show(bool value) override { visible_ = value; }
    bool is_visible() const override { return visible_; }
    void enable(bool value) override { enabled_ = value; }
    bool is_enabled() const override { return enabled_; }
    void set_title(std::string_view value) override { title_ = value; }
    std::string title() const override { return title_; }

    void focus() override { focused_ = true; }
    bool has_focus() const override { return focused_; }
    void capture(bool) override {}
    void set_cursor(platform::cursor) override {}
    core::pointi cursor_location() const override { return {}; }
    core::pointi client_to_screen(core::pointi point) const override { return point; }
    core::pointi screen_to_client(core::pointi point) const override { return point; }
    double scale() const override { return 1.0; }
    void set_resize_scale(double) override {}

    void invalidate(core::recti = {}) override {}
    void update_now() override {}
    void scroll(int, int, core::recti) override {}
    void start_timer(uint32_t, uint32_t) override {}
    void stop_timer(uint32_t) override {}
    bool is_minimized() const override { return false; }
    bool is_maximized() const override { return false; }
    bool is_occluded() const override { return false; }
    void sys_command(platform::sys_command_type) override {}
    void full_screen(bool value) override { fullscreen = value; }
    bool is_full_screen() const override { return fullscreen; }
    void set_text_input(bool, core::recti) override {}
    void save_placement(const platform::setting_file_ptr&, std::string_view) override {}

    core::recti bounds_;
    std::string title_;
    bool visible_ = true;
    bool enabled_ = true;
    bool focused_ = true;
    bool fullscreen = false;
    bool destroyed = false;
};

}  // namespace

int RunWindowChecks() {
    int checks = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) throw std::runtime_error(message);
    };

    auto frame = std::make_shared<FakeWindow>();
    zp::Window window;
    check(window.Attach(frame), "window attaches to a shared platform window");
    const auto events = window.EventSink();
    check(events != nullptr, "window exposes its platform event sink");

    window.NewFrame();
    events->key_down(static_cast<int>(platform::keys::RIGHT), {}, false);
    check(window.Down(zp::KEY_RIGHT) && window.Pressed(zp::KEY_RIGHT),
          "fresh key down is visible and pressed");
    window.NewFrame();
    events->key_down(static_cast<int>(platform::keys::RIGHT), {}, false);
    check(window.Down(zp::KEY_RIGHT) && !window.Pressed(zp::KEY_RIGHT),
          "held key is not pressed twice");

    events->key_up(static_cast<int>(platform::keys::RIGHT), {}, false);
    check(window.Released(zp::KEY_RIGHT), "key up is released in the same frame");

    window.NewFrame();
    events->key_down(static_cast<int>(platform::keys::LEFT), {}, false);
    events->key_up(static_cast<int>(platform::keys::LEFT), {}, false);
    check(!window.Down(zp::KEY_LEFT) && window.Pressed(zp::KEY_LEFT),
          "a down/up tap is latched for the frame");
    window.NewFrame();
    check(!window.Pressed(zp::KEY_LEFT), "tap latch expires at the next frame");

    platform::key_state alt;
    alt.alt = true;
    check(!events->key_down(static_cast<int>(platform::keys::SPACE), alt, true) &&
              window.Down(zp::KEY_FIRE),
          "system keys update gameplay state but remain unhandled");
    check(!events->key_up(static_cast<int>(platform::keys::SPACE), alt, true) &&
              !window.Down(zp::KEY_FIRE),
          "system key release remains available to Windows");

    window.NewFrame();
    check(!window.QueueTap(zp::KEY_RIGHT) && !window.Pressed(zp::KEY_RIGHT),
          "synthetic taps require a native platform window message queue");

    events->key_down('Q', {}, false);
    events->focus_changed(false);
    check(!window.Down(zp::KEY_SHIELD) && !window.Pressed(zp::KEY_SHIELD),
          "focus loss clears held and latched input");
    window.ToggleFullscreen();
    check(frame->fullscreen, "fullscreen toggles through the platform window");
    window.ToggleFullscreen();
    check(!frame->fullscreen, "fullscreen toggles back through the platform window");

    window.Destroy();
    check(frame->destroyed, "destroy releases the native platform window");
    check(window.EventSink() == nullptr, "destroy releases the event sink");
    return checks;
}

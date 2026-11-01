#include "../src/Window.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace {

class FakeFrame final : public pf::window_frame {
public:
    pf::frame_reactor_ptr reactor;
    bool closed = false;
    bool fullscreen = false;

    void set_reactor(pf::frame_reactor_ptr value) override { reactor = std::move(value); }
    void notify_size() override {}
    pf::irect get_client_rect() const override { return {0, 0, 1600, 900}; }
    void invalidate() override {}
    void invalidate_rect(const pf::irect&) override {}
    void set_focus() override {}
    bool has_focus() const override { return true; }
    void set_capture() override {}
    void release_capture() override {}
    uint32_t set_timer(uint32_t id, uint32_t) override { return id; }
    void kill_timer(uint32_t) override {}
    pf::ipoint screen_to_client(pf::ipoint point) const override { return point; }
    void set_cursor_shape(pf::cursor_shape) override {}
    void move_window(const pf::irect&) override {}
    void show(bool) override {}
    bool is_visible() const override { return true; }
    void set_text(std::string_view) override {}
    std::string text_from_clipboard() override { return {}; }
    bool text_to_clipboard(std::string_view) override { return true; }
    placement get_placement() const override { return {}; }
    void set_placement(const placement&) override {}
    void set_minimum_client_size(pf::isize) override {}
    void set_fullscreen(bool value) override { fullscreen = value; }
    bool is_fullscreen() const override { return fullscreen; }
    void toggle_fullscreen() override { fullscreen = !fullscreen; }
    void track_mouse_leave() override {}
    bool is_key_down(unsigned int) const override { return false; }
    bool is_key_down_async(unsigned int) const override { return false; }
    pf::window_frame_ptr create_child(std::string_view, uint32_t, pf::color_t) const & override {
        return nullptr;
    }
    void close() override { closed = true; }
    int message_box(std::string_view, std::string_view, uint32_t) override { return 0; }
    void present_pixels(const uint32_t*, int, int) override {}
    void set_menu(std::vector<pf::menu_command>) override {}
    std::unique_ptr<pf::measure_context> create_measure_context() const override { return nullptr; }
    void show_popup_menu(const std::vector<pf::menu_command>&, const pf::ipoint&) override {}
    double get_dpi_scale() const override { return 1.0; }
    void accept_drop_files(bool) override {}
    pf::toolbar_frame_ptr create_address_bar(const pf::address_bar_config&) override { return nullptr; }

    void Key(uint32_t vk, bool down, bool repeat = false) {
        if (!reactor) throw std::runtime_error("fake frame has no reactor");
        pf::keyboard_params p;
        p.vk = vk;
        p.repeat = repeat;
        reactor->handle_keyboard(shared_from_this(), down ? pf::keyboard_message_type::key_down
                                                         : pf::keyboard_message_type::key_up, p);
    }

    void LostFocus() {
        if (!reactor) throw std::runtime_error("fake frame has no reactor");
        pf::message_params p;
        reactor->handle_message(shared_from_this(), pf::message_type::kill_focus, p);
    }

    std::shared_ptr<FakeFrame> shared_from_this() {
        return self.lock();
    }

    std::weak_ptr<FakeFrame> self;
};

}  // namespace

int RunWindowChecks() {
    int checks = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) throw std::runtime_error(message);
    };

    auto frame = std::make_shared<FakeFrame>();
    frame->self = frame;
    zp::Window window;
    check(window.Attach(frame), "window attaches to a shared frame");

    window.NewFrame();
    frame->Key(pf::platform_key::Right, true);
    check(window.Down(zp::KEY_RIGHT) && window.Pressed(zp::KEY_RIGHT),
          "fresh key down is visible and pressed");
    window.NewFrame();
    check(window.Down(zp::KEY_RIGHT) && !window.Pressed(zp::KEY_RIGHT),
          "held key is not pressed twice");

    frame->Key(pf::platform_key::Right, false);
    check(window.Released(zp::KEY_RIGHT), "key up is released in the same frame");

    window.NewFrame();
    frame->Key(pf::platform_key::Left, true);
    frame->Key(pf::platform_key::Left, false);
    check(!window.Down(zp::KEY_LEFT) && window.Pressed(zp::KEY_LEFT),
          "a down/up tap is latched for the frame");
    window.NewFrame();
    check(!window.Pressed(zp::KEY_LEFT), "tap latch expires at the next frame");

    frame->Key('Q', true);
    frame->LostFocus();
    check(!window.Down(zp::KEY_SHIELD) && !window.Pressed(zp::KEY_SHIELD),
          "focus loss clears held and latched input");
    window.ToggleFullscreen();
    check(frame->fullscreen, "fullscreen toggles through the shared frame");
    window.ToggleFullscreen();
    check(!frame->fullscreen, "fullscreen toggles back through the shared frame");

    window.NewFrame();
    check(!window.QueueTap(zp::KEY_RIGHT) && !window.Pressed(zp::KEY_RIGHT),
          "synthetic taps require the shared backend event hook");
    window.Destroy();
    check(!frame->reactor, "destroy detaches the reactor before the owner dies");
    return checks;
}

// libdesktop-core contains its WinMain in the same static archive as the
// neutral event pump. The headless unit executable never enters that path, but
// its callback symbols still have to be satisfied at link time.
app_init_result app_init(const pf::window_frame_ptr&, std::span<const std::string_view>) {
    return {false, 0};
}

void app_idle() {}
void app_destroy() {}

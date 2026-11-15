// zpilot - the game's keys, read from the shell's keyboard state.
#pragma once

#include <libdesktop/ui.h>
#include <libdesktop/ui/shell.h>

namespace zp
{

	enum Key
	{
		// clang-format off
		KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
		KEY_FIRE, KEY_TRACTOR, KEY_SHIELD, KEY_SLOW,
		KEY_START, KEY_PAUSE, KEY_RESTART, KEY_QUIT,
		KEY_HELP, KEY_MUTE, KEY_FULLSCREEN, KEY_SHOT,
		// clang-format on
		KEY_COUNT
	};

	// key_input holds any code below 256. The pointer's buttons take two that no
	// keyboard sends, so a click is held, pressed and tapped like any key.
	constexpr char32_t kPointerFire = 0x01;
	constexpr char32_t kPointerTractor = 0x02;

	// Each game key answers to every physical key bound to it: the arrows and WASD
	// steer, Space and the left button fire. What is held and pressed is counted
	// per step by the shell, so a tap that goes down and up between two steps
	// still presses.
	class Input
	{
	public:
		explicit Input(const ui::key_input& keys) : keys_(&keys) {}

		bool Down(Key k) const;
		bool Pressed(Key k) const;

		// The code a script holds for a game key: its first binding.
		static char32_t Code(Key k);
		// The game key a code drives, or KEY_COUNT.
		static Key KeyFor(char32_t code);

	private:
		const ui::key_input* keys_;
	};

} // namespace zp

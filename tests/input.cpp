#include "../src/Input.h"

#include <stdexcept>

int RunInputChecks()
{
	int checks = 0;
	const auto check = [&](bool ok, const char* message)
	{
		++checks;
		if (!ok) throw std::runtime_error(message);
	};

	ui::key_input keys;
	const zp::Input input(keys);

	keys.down(platform::keys::RIGHT);
	check(input.Down(zp::KEY_RIGHT) && input.Pressed(zp::KEY_RIGHT), "a fresh key down is held and pressed");
	keys.next_step();
	keys.down(platform::keys::RIGHT);
	check(input.Down(zp::KEY_RIGHT) && !input.Pressed(zp::KEY_RIGHT), "a held key is not pressed twice");
	keys.up(platform::keys::RIGHT);
	check(!input.Down(zp::KEY_RIGHT), "a key let go is no longer held");

	keys.next_step();
	keys.down('A');
	keys.up('A');
	check(!input.Down(zp::KEY_LEFT) && input.Pressed(zp::KEY_LEFT),
	      "a tap between two steps still presses its game key");
	keys.next_step();
	check(!input.Pressed(zp::KEY_LEFT), "the tap is spent by the step after");

	keys.down(platform::keys::UP);
	keys.down('W');
	keys.up(platform::keys::UP);
	check(input.Down(zp::KEY_UP), "a game key stays held while any of its bindings is");

	keys.next_step();
	keys.down(zp::kPointerFire);
	check(input.Pressed(zp::KEY_FIRE) && input.Down(zp::KEY_FIRE), "the left button fires");
	keys.down(zp::kPointerTractor);
	check(input.Down(zp::KEY_TRACTOR), "the right button holds the tractor");

	keys.release_all();
	check(!input.Down(zp::KEY_FIRE) && !input.Down(zp::KEY_TRACTOR) && !input.Down(zp::KEY_UP),
	      "losing the keyboard lets every key and button go");

	// A code bound to two game keys would leave one of them unreachable.
	for (int k = 0; k < zp::KEY_COUNT; ++k)
	{
		const auto key = static_cast<zp::Key>(k);
		check(zp::Input::KeyFor(zp::Input::Code(key)) == key, "each game key's script code drives that key");
	}
	check(zp::Input::KeyFor('Z') == zp::KEY_COUNT, "an unbound key drives nothing");
	return checks;
}

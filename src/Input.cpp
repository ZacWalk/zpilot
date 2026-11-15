#include "Input.h"

namespace zp
{

	namespace
	{

		struct Binding
		{
			char32_t codes[3] = {};
			int count = 0;
		};

		// Read at call time: the platform's key codes are values it fills in, not
		// constants.
		Binding BindingFor(const Key key)
		{
			namespace keys = platform::keys;
			switch (key)
			{
			case KEY_LEFT: return {{keys::LEFT, 'A'}, 2};
			case KEY_RIGHT: return {{keys::RIGHT, 'D'}, 2};
			case KEY_UP: return {{keys::UP, 'W'}, 2};
			case KEY_DOWN: return {{keys::DOWN, 'S'}, 2};
			case KEY_FIRE: return {{keys::SPACE, kPointerFire}, 2};
			case KEY_TRACTOR: return {{keys::SHIFT, 'E', kPointerTractor}, 3};
			case KEY_SHIELD: return {{keys::CONTROL, 'Q'}, 2};
			case KEY_SLOW: return {{keys::TAB, 'F'}, 2};
			case KEY_START: return {{keys::RETURN}, 1};
			case KEY_PAUSE: return {{'P'}, 1};
			case KEY_RESTART: return {{'R'}, 1};
			case KEY_QUIT: return {{keys::ESCAPE}, 1};
			case KEY_HELP: return {{keys::F1}, 1};
			case KEY_MUTE: return {{keys::F3}, 1};
			case KEY_FULLSCREEN: return {{keys::F11}, 1};
			case KEY_SHOT: return {{keys::F12}, 1};
			default: return {};
			}
		}

	} // namespace

	bool Input::Down(const Key k) const
	{
		const Binding b = BindingFor(k);
		for (int i = 0; i < b.count; ++i)
			if (keys_->held(b.codes[i])) return true;
		return false;
	}

	bool Input::Pressed(const Key k) const
	{
		const Binding b = BindingFor(k);
		for (int i = 0; i < b.count; ++i)
			if (keys_->pressed(b.codes[i])) return true;
		return false;
	}

	char32_t Input::Code(const Key k)
	{
		const Binding b = BindingFor(k);
		return b.count > 0 ? b.codes[0] : 0;
	}

	Key Input::KeyFor(const char32_t code)
	{
		for (int k = 0; k < KEY_COUNT; ++k)
		{
			const Binding b = BindingFor(static_cast<Key>(k));
			for (int i = 0; i < b.count; ++i)
				if (b.codes[i] == code) return static_cast<Key>(k);
		}
		return KEY_COUNT;
	}

} // namespace zp

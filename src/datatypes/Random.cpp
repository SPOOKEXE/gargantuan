#include "gargantuan/datatypes/Random.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <chrono>
#include <common.hpp>
#include <exponential.hpp>
#include <glm/glm.hpp>
#include <gtc/constants.hpp>
#include <lua.h>
#include <lualib.h>
#include <random>
#include <trigonometric.hpp>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Random);
	G_UD_IMPL_PROPS(Random)
	G_UD_IMPL_METHODS(
		Random,
		G_UD_METHOD(Random, NextUnitVector),
		G_UD_METHOD(Random, Clone),
		{"NextInteger", {Random::LNextInteger}},
		{"NextNumber", {Random::LNextNumber}},
		{"Shuffle", {Random::LShuffle}}
	)

	static std::uint64_t Rotl(std::uint64_t x, int k) {
		return (x << k) | (x >> (64 - k));
	};

	static std::uint64_t SplitMix(std::uint64_t &x) {
		std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	};

	Random::Random(std::int64_t seed) {
		std::uint64_t state = static_cast<std::uint64_t>(seed);
		State[0] = SplitMix(state);
		State[1] = SplitMix(state);
		State[2] = SplitMix(state);
		State[3] = SplitMix(state);
	};

	static std::int64_t EntropySeed() {
		auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		return static_cast<std::int64_t>(std::random_device{}()) ^ static_cast<std::int64_t>(now);
	};

	Random::Random() : Random(EntropySeed()) {};

	std::uint64_t Random::NextBits() {
		std::uint64_t result = Rotl(State[1] * 5, 7) * 9;
		std::uint64_t t = State[1] << 17;

		State[2] ^= State[0];
		State[3] ^= State[1];
		State[1] ^= State[2];
		State[0] ^= State[3];
		State[2] ^= t;
		State[3] = Rotl(State[3], 45);

		return result;
	};

	double Random::NextDouble() {
		return (NextBits() >> 11) * 0x1.0p-53;
	};

	std::int64_t Random::NextInt(std::int64_t min, std::int64_t max) {
		std::uint64_t range = static_cast<std::uint64_t>(max) - static_cast<std::uint64_t>(min) + 1;
		if (range == 0) {
			return static_cast<std::int64_t>(NextBits());
		}

		std::uint64_t threshold = (0 - range) % range;
		std::uint64_t bits = NextBits();
		while (bits < threshold) {
			bits = NextBits();
		}

		return static_cast<std::int64_t>(static_cast<std::uint64_t>(min) + (bits % range));
	};

	glm::vec3 Random::NextUnitVector() {
		float z = static_cast<float>(NextDouble()) * 2.0f - 1.0f;
		float theta = static_cast<float>(NextDouble()) * glm::two_pi<float>();
		float radius = glm::sqrt(glm::max(1.0f - z * z, 0.0f));

		return {radius * glm::cos(theta), radius * glm::sin(theta), z};
	};

	Random Random::Clone() const {
		return *this;
	};

	int Random::LNextInteger(lua_State *L, Random *self) {
		std::int64_t min = static_cast<std::int64_t>(glm::floor(luaL_checknumber(L, 2)));
		std::int64_t max = static_cast<std::int64_t>(glm::floor(luaL_checknumber(L, 3)));
		if (min > max) {
			luaL_error(L, "max must be greater than or equal to min");
		}

		lua_pushnumber(L, static_cast<double>(self->NextInt(min, max)));
		return 1;
	}

	int Random::LNextNumber(lua_State *L, Random *self) {
		double value = self->NextDouble();
		if (lua_isnoneornil(L, 2)) {
			lua_pushnumber(L, value);
			return 1;
		}

		double min = luaL_checknumber(L, 2);
		double max = luaL_checknumber(L, 3);
		lua_pushnumber(L, min + value * (max - min));
		return 1;
	}

	int Random::LShuffle(lua_State *L, Random *self) {
		luaL_checktype(L, 2, LUA_TTABLE);

		int length = static_cast<int>(lua_objlen(L, 2));

		// shuffling acros a nil hole could change the table's length so we reject it
		for (int i = 1; i <= length; i++) {
			lua_rawgeti(L, 2, i);
			bool isHole = lua_isnil(L, -1);
			lua_pop(L, 1);

			if (isHole) {
				luaL_error(L, "table has a nil hole at index %d", i);
			}
		}

		for (int i = length; i > 1; i--) {
			int j = static_cast<int>(self->NextInt(1, i));
			if (i == j) {
				continue;
			}

			lua_rawgeti(L, 2, i);
			lua_rawgeti(L, 2, j);
			lua_rawseti(L, 2, i);
			lua_rawseti(L, 2, j);
		}

		return 0;
	}
} // namespace gargantuan

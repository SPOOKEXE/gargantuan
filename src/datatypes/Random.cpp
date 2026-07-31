#include "gargantuan/datatypes/Random.hpp"

#include <SDL3/SDL_timer.h>
#include "gargantuan/scripting/Userdata.hpp"

#include <chrono>
#include <glm/gtc/constants.hpp>
#include <lualib.h>

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Random);
	G_UD_IMPL_PROPS(Random)
	G_UD_IMPL_METHODS(
		Random,
		G_UD_METHOD(Random, NextInteger),
		G_UD_METHOD(Random, NextUnitVector),
		G_UD_METHOD(Random, Clone),
		// NextNumber takes either no arguments or a range, so it is bound by hand
		{"NextNumber",
		 {Random::LNextNumber, []() -> std::string { return "(self, min: number?, max: number?): number"; }}},
		{"Shuffle", {Random::LShuffle, []() -> std::string { return "(self, list: { any }): ()"; }}},
		{"__tostring", {Random::LTostring}}
	)

	// The performance counter rather than a wall clock: it ticks far faster, so
	// two Randoms built in the same millisecond do not come out identical.
	Random::Random() : Generator(SDL_GetPerformanceCounter()) {}

	Random::Random(int64_t seed) : Generator(static_cast<uint64_t>(seed)) {}

	int Random::NextInteger(int min, int max) {
		if (min > max) {
			std::swap(min, max);
		}
		return std::uniform_int_distribution<int>(min, max)(Generator);
	}

	double Random::NextNumber() {
		return std::uniform_real_distribution<double>(0.0, 1.0)(Generator);
	}

	double Random::NextNumberRange(double min, double max) {
		if (min > max) {
			std::swap(min, max);
		}
		return std::uniform_real_distribution<double>(min, max)(Generator);
	}

	// Marsaglia's method: uniform over the sphere, not over the cube
	glm::vec3 Random::NextUnitVector() {
		double z = NextNumberRange(-1.0, 1.0);
		double angle = NextNumberRange(0.0, 2.0 * glm::pi<double>());
		double radius = glm::sqrt(1.0 - z * z);
		return glm::vec3(radius * glm::cos(angle), radius * glm::sin(angle), z);
	}

	Random Random::Clone() const {
		return *this;
	}

	int Random::LNextNumber(lua_State *L, Random *self) {
		if (lua_isnoneornil(L, 2)) {
			lua_pushnumber(L, self->NextNumber());
			return 1;
		}

		double min = luaL_checknumber(L, 2);
		double max = luaL_checknumber(L, 3);
		lua_pushnumber(L, self->NextNumberRange(min, max));
		return 1;
	}

	// Fisher-Yates over the array portion of the table, shuffled in place
	int Random::LShuffle(lua_State *L, Random *self) {
		luaL_checktype(L, 2, LUA_TTABLE);

		int length = lua_objlen(L, 2);
		for (int i = length; i > 1; i--) {
			int j = self->NextInteger(1, i);
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

	int Random::LTostring(lua_State *L, Random *self) {
		lua_pushstring(L, "Random");
		return 1;
	}
}

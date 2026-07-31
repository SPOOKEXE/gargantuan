#pragma once

#include "gargantuan/scripting/Userdata.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <lua.h>
#include <random>

namespace gargantuan {
	struct Random : public Userdata<Random> {
	  public:
		G_UD_DECL_PRELUDE(Random);

		Random();
		Random(std::int64_t seed);

		Random Clone() const;
		int NextInteger(int min, int max);
		double NextNumber(double min = 0.0, double max = 1.0);
		glm::vec3 NextUnitVector();
		static int LNextNumber(lua_State *L, Random *self);
		static int LShuffle(lua_State *L, Random *self);

	  private:
		std::mt19937_64 Generator;
		std::uint64_t NextBits();
	};

	G_UD_STACKVALUE(Random);
}

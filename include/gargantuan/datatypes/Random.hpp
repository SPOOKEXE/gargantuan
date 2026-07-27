#pragma once

#include "gargantuan/scripting/Userdata.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	struct Random : public Userdata<Random> {
	  public:
		G_UD_DECL_PRELUDE(Random)

		std::uint64_t State[4];

		Random();
		Random(std::int64_t seed);

		std::uint64_t NextBits();
		double NextDouble();
		std::int64_t NextInt(std::int64_t min, std::int64_t max);

		glm::vec3 NextUnitVector();
		Random Clone() const;

		static int LNextInteger(lua_State *L, Random *self);
		static int LNextNumber(lua_State *L, Random *self);
		static int LShuffle(lua_State *L, Random *self);
	};

	G_UD_STACKVALUE(Random);
} // namespace gargantuan
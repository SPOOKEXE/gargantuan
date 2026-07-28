#pragma once

#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <lua.h>
#include <random>

namespace gargantuan {
	// NOTE: a Random is stored inline in its userdata, so methods mutate the
	// same generator the script is holding rather than a copy
	struct Random : public Userdata<Random> {
	  public:
		G_UD_DECL_PRELUDE(Random)

		Random();
		explicit Random(int64_t seed);

		int NextInteger(int min, int max);
		double NextNumber();
		double NextNumberRange(double min, double max);
		glm::vec3 NextUnitVector();
		Random Clone() const;

		static int LNextNumber(lua_State *L, Random *self);
		static int LShuffle(lua_State *L, Random *self);
		static int LTostring(lua_State *L, Random *self);

	  private:
		std::mt19937_64 Generator;
	};

	G_UD_STACKVALUE(Random);
} // namespace gargantuan

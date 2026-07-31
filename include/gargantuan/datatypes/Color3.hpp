#pragma once

#include "gargantuan/scripting/Userdata.hpp"

#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	struct Color3 : public Userdata<Color3> {
	  public:
		G_UD_DECL_PRELUDE(Color3)

		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;

		Color3();
		Color3(float r, float g, float b);

		static Color3 fromRGB(float r, float g, float b);
		static Color3 fromHSV(float h, float s, float v);
		static Color3 fromHex(std::string_view hex);

		operator glm::vec3() const {
			return {R, G, B};
		}
	};

	G_UD_STACKVALUE(Color3);
} // namespace gargantuan

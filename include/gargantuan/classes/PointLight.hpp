#pragma once

#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Instance.hpp"

namespace gargantuan {
	class PointLight : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		gargantuan::Color3 Color{1.0f, 1.0f, 1.0f};
		float Brightness = 1.0f;
		float Range = 8.0f;
		bool Enabled = true;
		bool Shadows = false;
	};
}

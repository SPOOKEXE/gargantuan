#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include <memory>

namespace gargantuan {

	class Workspace : public WorldRoot {
	  public:
		static const ClassDefinition DEFINITION;

		std::shared_ptr<Camera> CurrentCamera = std::make_shared<Camera>();

		float Gravity = 196.2f;
		double DistributedGameTime = 0.0;
		float FallenPartsDestroyHeight = -500.0f;
	};

}

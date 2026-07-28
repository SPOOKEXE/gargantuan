#pragma once

#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include <memory>

namespace gargantuan {

	class Workspace : public WorldRoot {
	  public:
		static const ClassDefinition DEFINITION;

		std::shared_ptr<Camera> CurrentCamera = std::make_shared<Camera>();

		// Roblox's default gravity, in studs per second squared
		float Gravity = 196.2f;
		// Seconds this place has been running, accumulated by the engine loop
		double DistributedGameTime = 0.0;
		float FallenPartsDestroyHeight = -500.0f;
	};

} // namespace gargantuan

#pragma once

#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include <memory>

namespace gargantuan {
	class TweenService : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		Tween::Pointer Create(Instance::Pointer instance, TweenInfo tweenInfo, Tween::GoalPropertyMap goalProperties) {
			auto tween = std::make_shared<Tween>(instance, tweenInfo, goalProperties);

			// do... stuff?

			return tween;
		}
	};
}

#pragma once

#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/math/EasingCurves.hpp"

#include <lua.h>
#include <memory>
#include <vector>

namespace gargantuan {
	class TweenService : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		std::shared_ptr<Tween>
		Create(lua_State *L, Instance::Pointer target, TweenInfo tweenInfo, Tween::GoalPropertyMap goalProperties);

		void Step(float deltaTime);

		float GetValue(float alpha, Enums::EasingStyle style, Enums::EasingDirection direction) const;

		static int LCreate(lua_State *L, Instance *instance);

	  private:
		std::vector<std::shared_ptr<Tween>> ActiveTweens;
	};
}

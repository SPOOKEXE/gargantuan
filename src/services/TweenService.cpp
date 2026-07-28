#include "gargantuan/services/TweenService.hpp"
#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/math/EasingCurves.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <lualib.h>
#include <string>

namespace gargantuan {
	const TweenService::ClassDefinition TweenService::DEFINITION = {
		.Name = "TweenService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<TweenService>(),
		.Methods = {
			{"Create",
			 {&TweenService::LCreate,
			  []() -> std::string {
				  return "(self, instance: Instance, tweenInfo: TweenInfo, goalProperties: { [string]: any }): Tween";
			  }}},
			G_UD_METHOD(TweenService, GetValue),
		}
	};

	std::shared_ptr<Tween> TweenService::Create(
		lua_State *L, Instance::Pointer target, TweenInfo tweenInfo, Tween::GoalPropertyMap goalProperties
	) {
		auto tween = std::make_shared<Tween>(L, std::move(target), tweenInfo, std::move(goalProperties));
		ActiveTweens.push_back(tween);
		return tween;
	}

	void TweenService::Step(float deltaTime) {
		// A tween's Completed signal can run Luau, which can create or cancel
		// tweens, so step a snapshot rather than the live list
		auto stepping = ActiveTweens;
		for (auto &tween : stepping) {
			tween->Step(deltaTime);
		}

		std::erase_if(ActiveTweens, [](const std::shared_ptr<Tween> &tween) { return tween->IsFinished(); });
	}

	float TweenService::GetValue(float alpha, Enums::EasingStyle style, Enums::EasingDirection direction) const {
		return EasingCurves::CalculateAlpha(glm::clamp(alpha, 0.0f, 1.0f), style, direction);
	}

	int TweenService::LCreate(lua_State *L, Instance *instance) {
		auto *self = instance->Cast<TweenService>();
		if (!self) {
			luaL_error(L, "Create must be called on a TweenService");
			return 0;
		}

		Instance::Pointer target = CheckStackValue<Instance::Pointer>(L, 2);
		TweenInfo tweenInfo = CheckStackValue<TweenInfo>(L, 3);
		luaL_checktype(L, 4, LUA_TTABLE);

		Tween::GoalPropertyMap goalProperties;

		lua_pushnil(L);
		while (lua_next(L, 4) != 0) {
			// Only read the key once its type is known -- lua_tolstring would
			// otherwise rewrite a numeric key in place and derail lua_next
			if (lua_type(L, -2) != LUA_TSTRING) {
				luaL_error(L, "Tween goal property names must be strings");
			}

			size_t length;
			const char *rawName = lua_tolstring(L, -2, &length);
			std::string propertyName(rawName, length);

			if (!target->FindProperty(propertyName).has_value()) {
				luaL_error(L, "%s is not a valid property of %s", propertyName.c_str(), target->GetFullName().c_str());
			}

			auto value = Tween::ReadTweenableValue(L, -1);
			if (!value.has_value()) {
				luaL_error(L, "Cannot tween property %s, its type is not tweenable", propertyName.c_str());
			}

			goalProperties.insert_or_assign(std::move(propertyName), std::move(value.value()));
			lua_pop(L, 1);
		}

		auto tween = self->Create(L, target, tweenInfo, std::move(goalProperties));
		StackValue<Instance::Pointer>::Push(L, tween);
		return 1;
	}
}

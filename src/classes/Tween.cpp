#include "gargantuan/classes/Tween.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/math/EasingCurves.hpp"
#include "gargantuan/math/LerpValue.hpp"
#include "gargantuan/scripting/StackGuard.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	// Guards against dividing by a zero-length tween
	static constexpr float MINIMUM_DURATION = 1e-6f;

	const Tween::ClassDefinition Tween::DEFINITION = {
		.Name = "Tween",
		.Superclass = "Instance",
		.Properties =
			{
				{
					"Instance",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<Instance::Pointer>::Push(L, instance->Cast<Tween>()->Target);
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(Instance::Pointer),
					},
				},
				{
					"TweenInfo",
					{
						[](lua_State *L, Instance *instance) -> int {
							StackValue<gargantuan::TweenInfo>::Push(L, instance->Cast<Tween>()->Info);
							return 1;
						},
						nullptr,
						G_UD_REFLECT_TYPE(gargantuan::TweenInfo),
					},
				},
				G_UD_READONLY_PROP(Tween, PlaybackState, Enums::PlaybackState),
				G_UD_READONLY_PROP(Tween, Completed, Signal<Enums::PlaybackState>::Pointer),
			},
		.Methods = {
			G_UD_METHOD(Tween, Play),
			G_UD_METHOD(Tween, Pause),
			G_UD_METHOD(Tween, Cancel),
		}
	};

	Tween::Tween(lua_State *L, Instance::Pointer target, gargantuan::TweenInfo info, GoalPropertyMap goalProperties)
		: Target(std::move(target)), Info(info), GoalProperties(std::move(goalProperties)), L(L) {
		Name = DEFINITION.Name;
	};

	std::optional<Tween::TweenableValue> Tween::ReadTweenableValue(lua_State *L, int idx) {
		// Userdata datatypes are checked before numbers because lua_isnumber
		// also accepts numeric strings
		if (StackValue<CFrame>::Is(L, idx)) return TweenableValue(StackValue<CFrame>::From(L, idx));
		if (StackValue<Color3>::Is(L, idx)) return TweenableValue(StackValue<Color3>::From(L, idx));
		if (StackValue<Color4>::Is(L, idx)) return TweenableValue(StackValue<Color4>::From(L, idx));
		if (StackValue<UDim>::Is(L, idx)) return TweenableValue(StackValue<UDim>::From(L, idx));
		if (StackValue<UDim2>::Is(L, idx)) return TweenableValue(StackValue<UDim2>::From(L, idx));
		if (StackValue<Vector2>::Is(L, idx)) return TweenableValue(StackValue<Vector2>::From(L, idx));
		if (StackValue<glm::vec3>::Is(L, idx)) return TweenableValue(StackValue<glm::vec3>::From(L, idx));
		if (lua_isnumber(L, idx)) return TweenableValue((float)lua_tonumber(L, idx));
		return std::nullopt;
	}

	void Tween::CaptureInitialProperties() {
		InitialProperties.clear();
		if (!L || !Target) {
			return;
		}

		// Property reads leave a value behind; the guard makes sure a failing
		// read cannot leave the stack deeper than it found it
		StackGuard guard(L);
		guard.Reserve(2);

		for (auto &[name, goalValue] : GoalProperties) {
			auto property = Target->FindProperty(name);
			if (!property.has_value() || !property->Read) {
				continue;
			}

			std::visit(
				[&](auto &&goal) {
					using ValueType = std::decay_t<decltype(goal)>;

					property->Read(L, Target.get());
					// The property may not actually hold the same datatype the
					// goal does, in which case there is nothing to interpolate
					if (StackValue<ValueType>::Is(L, -1)) {
						InitialProperties[name] = StackValue<ValueType>::From(L, -1);
					}
					lua_pop(L, 1);
				},
				goalValue
			);
		}
	}

	void Tween::ApplyAlpha(float alpha) {
		if (!L || !Target) {
			return;
		}

		StackGuard guard(L);
		guard.Reserve(2);

		for (auto &[name, goalValue] : GoalProperties) {
			auto initial = InitialProperties.find(name);
			if (initial == InitialProperties.end()) {
				continue;
			}

			auto property = Target->FindProperty(name);
			if (!property.has_value() || !property->Write) {
				continue;
			}

			std::visit(
				[&](auto &&goal) {
					using ValueType = std::decay_t<decltype(goal)>;
					if (!std::holds_alternative<ValueType>(initial->second)) {
						return;
					}

					ValueType value = LerpValue<ValueType>::Lerp(std::get<ValueType>(initial->second), goal, alpha);
					StackValue<ValueType>::Push(L, value);
					// A tween writes properties without going through the
					// script path, so it has to say so itself
					Target->MarkChanged();
					property->Write(L, Target.get());
					lua_pop(L, 1);
				},
				goalValue
			);
		}
	}

	void Tween::Play() {
		if (PlaybackState == Enums::PlaybackState::Playing || PlaybackState == Enums::PlaybackState::Delayed) {
			return;
		}

		// Resuming a paused tween keeps its progress; anything else starts over
		// and re-reads the target's current values as the starting point
		if (PlaybackState != Enums::PlaybackState::Paused) {
			Elapsed = 0.0f;
			DelayElapsed = 0.0f;
			CaptureInitialProperties();
		}

		PlaybackState =
			DelayElapsed < Info.DelayTime ? Enums::PlaybackState::Delayed : Enums::PlaybackState::Playing;
	}

	void Tween::Pause() {
		if (PlaybackState != Enums::PlaybackState::Playing && PlaybackState != Enums::PlaybackState::Delayed) {
			return;
		}

		PlaybackState = Enums::PlaybackState::Paused;
	}

	void Tween::Cancel() {
		if (PlaybackState == Enums::PlaybackState::Cancelled) {
			return;
		}

		Elapsed = 0.0f;
		DelayElapsed = 0.0f;
		InitialProperties.clear();
		PlaybackState = Enums::PlaybackState::Cancelled;

		Completed->Fire(PlaybackState);
	}

	bool Tween::IsFinished() const {
		return PlaybackState == Enums::PlaybackState::Completed ||
			   PlaybackState == Enums::PlaybackState::Cancelled;
	}

	void Tween::Step(float deltaTime) {
		if (PlaybackState == Enums::PlaybackState::Delayed) {
			DelayElapsed += deltaTime;
			if (DelayElapsed < Info.DelayTime) {
				return;
			}

			// Roll the overshoot into the tween proper so a long frame doesn't
			// silently stretch the tween by up to one frame
			deltaTime = DelayElapsed - Info.DelayTime;
			DelayElapsed = Info.DelayTime;
			PlaybackState = Enums::PlaybackState::Playing;
		}

		if (PlaybackState != Enums::PlaybackState::Playing) {
			return;
		}

		float duration = glm::max(Info.Time, MINIMUM_DURATION);
		// Reversing doubles a cycle: out to the goal, then back to the start
		float cycleLength = Info.Reverses ? duration * 2.0f : duration;

		Elapsed += deltaTime;

		bool finished = false;
		float cyclePosition;
		if (Info.RepeatCount < 0) {
			// A negative repeat count loops forever
			cyclePosition = std::fmod(Elapsed, cycleLength);
		} else {
			float totalLength = cycleLength * (float)(Info.RepeatCount + 1);
			if (Elapsed >= totalLength) {
				Elapsed = totalLength;
				// Land exactly on the end of the final cycle rather than
				// wrapping back around to zero
				cyclePosition = cycleLength;
				finished = true;
			} else {
				cyclePosition = std::fmod(Elapsed, cycleLength);
			}
		}

		float progress = cyclePosition / duration;
		if (Info.Reverses && progress > 1.0f) {
			progress = 2.0f - progress;
		}

		ApplyAlpha(
			EasingCurves::CalculateAlpha(glm::clamp(progress, 0.0f, 1.0f), Info.EasingStyle, Info.EasingDirection)
		);

		if (finished) {
			PlaybackState = Enums::PlaybackState::Completed;
			Completed->Fire(PlaybackState);
		}
	}
}

#pragma once

#include "gargantuan/math/EasingCurves.hpp"
#include "gargantuan/scripting/Userdata.hpp"

#include <cstdint>
#include <lua.h>

namespace gargantuan {
	struct TweenInfo : public Userdata<TweenInfo> {
	  public:
		G_UD_DECL_PRELUDE(TweenInfo);

		using Pointer = std::shared_ptr<TweenInfo>;

		float Time = 1.0f;
		bool Reverses = false;
		int32_t RepeatCount = 0;
		float DelayTime = 0;
		Enums::EasingDirection EasingDirection = Enums::EasingDirection::Out;
		Enums::EasingStyle EasingStyle = Enums::EasingStyle::Quad;

		TweenInfo(
			float time = 1.0f,
			Enums::EasingStyle easingStyle = Enums::EasingStyle::Quad,
			Enums::EasingDirection easingDirection = Enums::EasingDirection::Out,
			int32_t repeatCount = 0,
			bool reverses = false,
			float delayTime = 0
		);
	};

	G_UD_STACKVALUE(TweenInfo);
}

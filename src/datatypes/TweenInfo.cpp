#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	G_UD_IMPL_PRELUDE(TweenInfo);
	G_UD_IMPL_PROPS(
		TweenInfo,
		{"Time", Property::fromSimple<&TweenInfo::Time>(true, false)},
		{"EasingStyle", Property::fromSimple<&TweenInfo::EasingStyle>(true, false)},
		{"EasingDirection", Property::fromSimple<&TweenInfo::EasingDirection>(true, false)},
		{"RepeatCount", Property::fromSimple<&TweenInfo::RepeatCount>(true, false)},
		{"Reverses", Property::fromSimple<&TweenInfo::Reverses>(true, false)},
		{"DelayTime", Property::fromSimple<&TweenInfo::DelayTime>(true, false)}
	)
	G_UD_IMPL_METHODS(TweenInfo)
	TweenInfo::TweenInfo(
		float time,
		Enums::EasingStyle easingStyle,
		Enums::EasingDirection easingDirection,
		int32_t repeatCount,
		bool reverses,
		float delayTime
	)
		: Time(time), EasingStyle(easingStyle), EasingDirection(easingDirection), RepeatCount(repeatCount),
		  Reverses(reverses), DelayTime(delayTime) {};
}

#include "gargantuan/datatypes/Axes.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	G_UD_IMPL_PRELUDE(Axes);
	G_UD_IMPL_PROPS(
		Axes,

		{"Top", Property::fromSimple<&Axes::Top>(true, false)},
		{"Bottom", Property::fromSimple<&Axes::Bottom>(true, false)},
		{"Left", Property::fromSimple<&Axes::Left>(true, false)},
		{"Right", Property::fromSimple<&Axes::Right>(true, false)},
		{"Front", Property::fromSimple<&Axes::Front>(true, false)},
		{"Back", Property::fromSimple<&Axes::Back>(true, false)},
		{"X", Property::fromRead([](Axes *self) { return self->Left && self->Right; })},
		{"Y", Property::fromRead([](Axes *self) { return self->Top && self->Bottom; })},
		{"Z", Property::fromRead([](Axes *self) { return self->Front && self->Back; })}
	)
	G_UD_IMPL_METHODS(Axes)

	void Axes::SetNormal(const Enums::NormalId &normal) {
		switch (normal) {
		case Enums::NormalId::Top:
			Top = true;
			return;
		case Enums::NormalId::Bottom:
			Bottom = true;
			return;
		case Enums::NormalId::Left:
			Left = true;
			return;
		case Enums::NormalId::Right:
			Right = true;
			return;
		case Enums::NormalId::Front:
			Front = true;
			return;
		case Enums::NormalId::Back:
			Back = true;
			return;
		}
	}
}

#pragma once

#include "gargantuan/reflection/Enums.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	G_ENUM(NormalId, Right, Top, Back, Left, Bottom, Front);

	struct Axes : public Userdata<Axes> {
	  public:
		G_UD_DECL_PRELUDE(Axes);

		bool Top = false;
		bool Bottom = false;
		bool Left = false;
		bool Right = false;
		bool Front = false;
		bool Back = false;

		void SetNormal(const Enums::NormalId &normal);
	};

	G_UD_STACKVALUE(Axes);
}

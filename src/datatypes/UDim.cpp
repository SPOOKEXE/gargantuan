#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	G_UD_IMPL_PRELUDE(UDim);
	G_UD_IMPL_PROPS(
		UDim,

		{"Scale", Property::fromSimple<&UDim::Scale>(true, false)},
		{"Offset", Property::fromSimple<&UDim::Offset>(true, false)}
	)
	G_UD_IMPL_METHODS(UDim)

	UDim::UDim(float scale, int offset) : Scale(scale), Offset(offset) {};

} // namespace gargantuan

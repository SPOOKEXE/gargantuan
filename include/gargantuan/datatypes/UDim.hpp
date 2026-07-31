#include "gargantuan/scripting/Userdata.hpp"
#include <glm/glm.hpp>
#include <lua.h>

namespace gargantuan {
	struct UDim : public Userdata<UDim> {
	  public:
		G_UD_DECL_PRELUDE(UDim);

		float Scale = 0.0f;
		int Offset = 0;

		UDim();
		UDim(float scale = 0.0f, int offset = 0);
	};

	G_UD_STACKVALUE(UDim);
}

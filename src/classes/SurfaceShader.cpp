#include "gargantuan/classes/SurfaceShader.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const SurfaceShader::ClassDefinition SurfaceShader::DEFINITION = {
		.Name = "SurfaceShader",
		.Superclass = "ShaderScript",
		.Constructor = ClassDefinition::WrapConstructor<SurfaceShader>(),
	};
}

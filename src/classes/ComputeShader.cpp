#include "gargantuan/classes/ComputeShader.hpp"
#include "gargantuan/datatypes/Vector3.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const ComputeShader::ClassDefinition ComputeShader::DEFINITION = {
		.Name = "ComputeShader",
		.Superclass = "ShaderScript",
		.Constructor = ClassDefinition::WrapConstructor<ComputeShader>(),
		.Properties = {
			G_UD_READWRITE_PROP(ComputeShader, ThreadGroupSize, glm::vec3),
		}
	};
}

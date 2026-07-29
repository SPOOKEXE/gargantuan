#include "gargantuan/classes/PostProcessShader.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const PostProcessShader::ClassDefinition PostProcessShader::DEFINITION = {
		.Name = "PostProcessShader",
		.Superclass = "ShaderScript",
		.Constructor = ClassDefinition::WrapConstructor<PostProcessShader>(),
	};
}

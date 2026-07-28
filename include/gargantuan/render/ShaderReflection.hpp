#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gargantuan::ShaderReflection {
	// Where one member of a shader's uniform block lives
	struct Member {
		uint32_t Offset = 0;
		// Bytes the member occupies. Writes are clamped to this so setting a
		// float cannot spill into whatever follows it.
		uint32_t Size = 0;
	};

	struct BlockLayout {
		// Declared member name -> where to write it
		std::unordered_map<std::string, Member> Members;
		// Total bytes to push for the block
		uint32_t Size = 0;
		// False when the SPIR-V could not be read, or had no such block
		bool Found = false;

		const Member *Find(const std::string &name) const;
	};

	// Reads the uniform block at `binding` out of a SPIR-V module. Gargantuan
	// puts a shader's own parameters at binding 1, with the engine builtins at
	// binding 0.
	//
	// Reflecting rather than trusting declaration order is what lets
	// SetNumber("Intensity", ...) land on the member actually called
	// Intensity, whatever position it sits in.
	BlockLayout ReflectUniformBlock(const void *spirv, size_t bytes, uint32_t binding);
} // namespace gargantuan::ShaderReflection

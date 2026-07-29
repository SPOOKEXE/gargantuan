#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gargantuan::ShaderReflection {
	struct Member {
		uint32_t Offset = 0;
		// Write bound in bytes; prevents spill into the next member.
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

	// Reflects a SPIR-V uniform block by binding and declared member name.
	BlockLayout ReflectUniformBlock(const void *spirv, size_t bytes, uint32_t binding);

	// Reflected resource counts used to build the exact pipeline layout.
	struct ResourceCounts {
		// sampler2D and friends
		uint32_t SampledImages = 0;
		// image2D declared readonly
		uint32_t ReadOnlyStorageImages = 0;
		// image2D declared writeonly or read-write
		uint32_t WriteStorageImages = 0;
		uint32_t UniformBuffers = 0;
		bool Found = false;
	};

	ResourceCounts ReflectResources(const void *spirv, size_t bytes);

	// Members actually read from the block; declarations alone do not invalidate caching.
	struct BlockUsage {
		std::unordered_set<std::string> ReadMembers;
		// False when SPIR-V is unreadable or the binding has no block.
		bool Found = false;

		bool Reads(const std::string &name) const;
	};

	// Unresolved accesses conservatively mark every member read to prevent stale frames.
	BlockUsage ReflectBlockUsage(const void *spirv, size_t bytes, uint32_t binding);
}

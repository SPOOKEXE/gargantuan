#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gargantuan::ShaderReflection {
	struct BlockMember {
		uint32_t OffsetBytes = 0;
		uint32_t SizeBytes = 0;
	};

	struct BlockLayout {
		std::unordered_map<std::string, BlockMember> Members;
		uint32_t SizeBytes = 0;
		bool WasBlockFound = false;
		bool AreMemberNamesStripped = false;

		const BlockMember *FindMember(const std::string &name) const;
	};

	BlockLayout ReflectUniformBlock(const void *spirv, size_t bytes, uint32_t binding);

	struct ResourceCounts {
		uint32_t SampledImages = 0;
		uint32_t ReadOnlyStorageImages = 0;
		uint32_t ReadWriteStorageImages = 0;
		uint32_t UniformBuffers = 0;
		bool WasSpirvParsed = false;
	};

	ResourceCounts ReflectResources(const void *spirv, size_t bytes);

	struct BlockUsage {
		std::unordered_set<std::string> ReadMembers;
		bool WasBlockFound = false;
		bool AreMemberNamesStripped = false;

		bool MayRead(const std::string &name) const;
	};

	BlockUsage ReflectBlockUsage(const void *spirv, size_t bytes, uint32_t binding);
}

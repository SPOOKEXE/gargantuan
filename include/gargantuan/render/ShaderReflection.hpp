#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

	// What a shader actually asks the pipeline for. Building a pipeline from
	// these rather than from a guess means a shader that declares two samplers
	// gets two, and a script that supplies the wrong number is told so instead
	// of failing somewhere inside the driver.
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

	// Which members of the block at `binding` a shader actually reads, as
	// opposed to merely declares. The two differ constantly: every gargantuan
	// shader declares the whole Builtin block because it is one layout, and
	// most of them only ever touch Resolution.
	//
	// Reading is what matters for the frame cache. A pass that reads Time
	// paints a different picture every frame and can never be cached; one that
	// only reads Resolution is as still as the scene it draws.
	struct BlockUsage {
		std::unordered_set<std::string> ReadMembers;
		// False when the SPIR-V could not be read, or had no block at that
		// binding. Both mean nothing is known to be read, which is the same
		// answer the engine had before it looked.
		bool Found = false;

		bool Reads(const std::string &name) const;
	};

	// Conservative upwards: an access it cannot follow counts as reading
	// everything, because over-reporting costs a cache that was not needed
	// while under-reporting freezes the picture.
	BlockUsage ReflectBlockUsage(const void *spirv, size_t bytes, uint32_t binding);
} // namespace gargantuan::ShaderReflection

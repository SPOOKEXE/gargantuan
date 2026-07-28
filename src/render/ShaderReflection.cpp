#include "gargantuan/render/ShaderReflection.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_set>

namespace gargantuan::ShaderReflection {
	namespace {
		constexpr uint32_t SPIRV_MAGIC = 0x07230203;
		constexpr size_t HEADER_WORDS = 5;

		// The handful of opcodes a uniform block's layout is described with
		constexpr uint16_t OP_MEMBER_NAME = 6;
		constexpr uint16_t OP_TYPE_INT = 21;
		constexpr uint16_t OP_TYPE_FLOAT = 22;
		constexpr uint16_t OP_TYPE_VECTOR = 23;
		constexpr uint16_t OP_TYPE_MATRIX = 24;
		constexpr uint16_t OP_TYPE_ARRAY = 28;
		constexpr uint16_t OP_TYPE_IMAGE = 25;
		constexpr uint16_t OP_TYPE_SAMPLED_IMAGE = 27;
		constexpr uint16_t OP_TYPE_STRUCT = 30;
		constexpr uint16_t OP_TYPE_POINTER = 32;
		constexpr uint16_t OP_VARIABLE = 59;
		constexpr uint16_t OP_DECORATE = 71;
		constexpr uint16_t OP_MEMBER_DECORATE = 72;

		// Enough to follow a `builtin.Time` read back to the member it names
		constexpr uint16_t OP_CONSTANT = 43;
		constexpr uint16_t OP_LOAD = 61;
		constexpr uint16_t OP_ACCESS_CHAIN = 65;
		constexpr uint16_t OP_IN_BOUNDS_ACCESS_CHAIN = 66;
		constexpr uint16_t OP_COPY_OBJECT = 83;

		constexpr uint32_t DECORATION_BINDING = 33;
		constexpr uint32_t DECORATION_OFFSET = 35;
		constexpr uint32_t DECORATION_MATRIX_STRIDE = 7;
		constexpr uint32_t DECORATION_ARRAY_STRIDE = 6;

		constexpr uint32_t DECORATION_NON_WRITABLE = 24;

		constexpr uint32_t STORAGE_CLASS_UNIFORM_CONSTANT = 0;
		constexpr uint32_t STORAGE_CLASS_UNIFORM = 2;

		struct TypeInfo {
			uint16_t Opcode = 0;
			uint32_t Width = 0;       // scalars
			uint32_t ComponentType = 0; // vectors and matrices
			uint32_t ComponentCount = 0;
			uint32_t Stride = 0; // matrices and arrays
		};

		// Reads a NUL-terminated, word-padded SPIR-V literal string
		std::string ReadString(const uint32_t *words, size_t wordCount, size_t &at) {
			std::string text;
			while (at < wordCount) {
				uint32_t word = words[at++];
				for (int byte = 0; byte < 4; byte++) {
					char character = (char)((word >> (byte * 8)) & 0xFF);
					if (character == '\0') {
						return text;
					}
					text += character;
				}
			}
			return text;
		}
	} // namespace

	const Member *BlockLayout::Find(const std::string &name) const {
		auto it = Members.find(name);
		return it == Members.end() ? nullptr : &it->second;
	}

	BlockLayout ReflectUniformBlock(const void *spirv, size_t bytes, uint32_t binding) {
		BlockLayout layout;

		if (!spirv || bytes < HEADER_WORDS * sizeof(uint32_t) || bytes % sizeof(uint32_t) != 0) {
			return layout;
		}

		const auto *words = static_cast<const uint32_t *>(spirv);
		size_t wordCount = bytes / sizeof(uint32_t);
		if (words[0] != SPIRV_MAGIC) {
			return layout;
		}

		// name and offset of every member, per struct type
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> memberNames;
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> memberOffsets;
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> memberStrides;
		std::unordered_map<uint32_t, uint32_t> variableBindings;
		std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers; // id -> (storage, type)
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables; // id -> (pointerType, storage)
		std::unordered_map<uint32_t, TypeInfo> types;
		std::unordered_set<uint32_t> nonWritable;

		for (size_t at = HEADER_WORDS; at < wordCount;) {
			uint32_t instruction = words[at];
			uint16_t opcode = (uint16_t)(instruction & 0xFFFF);
			uint16_t length = (uint16_t)(instruction >> 16);

			// A zero length would spin forever on a malformed module
			if (length == 0 || at + length > wordCount) {
				break;
			}

			const uint32_t *operands = words + at + 1;
			uint32_t operandCount = length - 1;

			switch (opcode) {
			case OP_MEMBER_NAME: {
				if (operandCount >= 3) {
					size_t cursor = at + 3;
					memberNames[operands[0]][operands[1]] = ReadString(words, at + length, cursor);
				}
				break;
			}
			case OP_MEMBER_DECORATE: {
				if (operandCount >= 4) {
					uint32_t decoration = operands[2];
					if (decoration == DECORATION_OFFSET) {
						memberOffsets[operands[0]][operands[1]] = operands[3];
					} else if (decoration == DECORATION_MATRIX_STRIDE) {
						memberStrides[operands[0]][operands[1]] = operands[3];
					}
				}
				break;
			}
			case OP_DECORATE: {
				if (operandCount >= 3 && operands[1] == DECORATION_BINDING) {
					variableBindings[operands[0]] = operands[2];
				} else if (operandCount >= 3 && operands[1] == DECORATION_ARRAY_STRIDE) {
					types[operands[0]].Stride = operands[2];
				} else if (operandCount >= 2 && operands[1] == DECORATION_NON_WRITABLE) {
					nonWritable.insert(operands[0]);
				}
				break;
			}
			case OP_TYPE_STRUCT: {
				if (operandCount >= 1) {
					std::vector<uint32_t> members(operands + 1, operands + operandCount);
					structMembers[operands[0]] = std::move(members);
					types[operands[0]].Opcode = OP_TYPE_STRUCT;
				}
				break;
			}
			case OP_TYPE_POINTER: {
				if (operandCount >= 3) {
					pointers[operands[0]] = {operands[1], operands[2]};
				}
				break;
			}
			case OP_VARIABLE: {
				// result type, result id, storage class
				if (operandCount >= 3) {
					variables[operands[1]] = {operands[0], operands[2]};
				}
				break;
			}
			case OP_TYPE_INT:
			case OP_TYPE_FLOAT: {
				if (operandCount >= 2) {
					types[operands[0]].Opcode = opcode;
					types[operands[0]].Width = operands[1];
				}
				break;
			}
			case OP_TYPE_VECTOR:
			case OP_TYPE_MATRIX: {
				if (operandCount >= 3) {
					auto &type = types[operands[0]];
					type.Opcode = opcode;
					type.ComponentType = operands[1];
					type.ComponentCount = operands[2];
				}
				break;
			}
			case OP_TYPE_IMAGE: {
				// result, sampled type, dim, depth, arrayed, ms, sampled, format
				if (operandCount >= 7) {
					auto &type = types[operands[0]];
					type.Opcode = OP_TYPE_IMAGE;
					// 1 means it is read through a sampler, 2 means storage
					type.ComponentCount = operands[6];
				}
				break;
			}
			case OP_TYPE_SAMPLED_IMAGE: {
				if (operandCount >= 2) {
					auto &type = types[operands[0]];
					type.Opcode = OP_TYPE_SAMPLED_IMAGE;
					type.ComponentType = operands[1];
				}
				break;
			}
			case OP_TYPE_ARRAY: {
				if (operandCount >= 2) {
					auto &type = types[operands[0]];
					type.Opcode = opcode;
					type.ComponentType = operands[1];
				}
				break;
			}
			default:
				break;
			}

			at += length;
		}

		// Find the uniform variable sitting at the requested binding, and walk
		// its pointer back to the struct type it points at
		uint32_t blockStruct = 0;
		for (const auto &[variableId, pointerAndStorage] : variables) {
			auto bindingIt = variableBindings.find(variableId);
			if (bindingIt == variableBindings.end() || bindingIt->second != binding) {
				continue;
			}

			auto pointerIt = pointers.find(pointerAndStorage.first);
			if (pointerIt == pointers.end() || pointerIt->second.first != STORAGE_CLASS_UNIFORM) {
				continue;
			}

			if (structMembers.count(pointerIt->second.second)) {
				blockStruct = pointerIt->second.second;
				break;
			}
		}

		if (blockStruct == 0) {
			return layout;
		}

		// How many bytes a member's own type occupies
		std::function<uint32_t(uint32_t, uint32_t)> sizeOf = [&](uint32_t typeId, uint32_t stride) -> uint32_t {
			auto it = types.find(typeId);
			if (it == types.end()) {
				return 16;
			}

			const TypeInfo &type = it->second;
			switch (type.Opcode) {
			case OP_TYPE_INT:
			case OP_TYPE_FLOAT:
				return std::max(type.Width / 8u, 1u);
			case OP_TYPE_VECTOR:
				return sizeOf(type.ComponentType, 0) * type.ComponentCount;
			case OP_TYPE_MATRIX:
				// std140 pads every column out to the matrix stride
				return (stride ? stride : 16) * type.ComponentCount;
			default:
				// Anything else is treated as one full slot, which is the most
				// a Set* call ever writes
				return 16;
			}
		};

		const auto &members = structMembers[blockStruct];
		const auto &names = memberNames[blockStruct];
		const auto &offsets = memberOffsets[blockStruct];
		const auto &strides = memberStrides[blockStruct];

		uint32_t end = 0;
		for (uint32_t index = 0; index < members.size(); index++) {
			auto offsetIt = offsets.find(index);
			auto nameIt = names.find(index);
			if (offsetIt == offsets.end() || nameIt == names.end()) {
				continue;
			}

			auto strideIt = strides.find(index);
			uint32_t size = sizeOf(members[index], strideIt == strides.end() ? 0 : strideIt->second);

			layout.Members[nameIt->second] = Member{offsetIt->second, size};
			end = std::max(end, offsetIt->second + size);
		}

		layout.Size = end;
		layout.Found = !layout.Members.empty();
		return layout;
	}

	ResourceCounts ReflectResources(const void *spirv, size_t bytes) {
		ResourceCounts counts;

		if (!spirv || bytes < HEADER_WORDS * sizeof(uint32_t) || bytes % sizeof(uint32_t) != 0) {
			return counts;
		}

		const auto *words = static_cast<const uint32_t *>(spirv);
		size_t wordCount = bytes / sizeof(uint32_t);
		if (words[0] != SPIRV_MAGIC) {
			return counts;
		}

		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables;
		std::unordered_map<uint32_t, TypeInfo> types;
		std::unordered_set<uint32_t> nonWritable;
		std::unordered_set<uint32_t> hasBinding;

		for (size_t at = HEADER_WORDS; at < wordCount;) {
			uint32_t instruction = words[at];
			uint16_t opcode = (uint16_t)(instruction & 0xFFFF);
			uint16_t length = (uint16_t)(instruction >> 16);

			if (length == 0 || at + length > wordCount) {
				break;
			}

			const uint32_t *operands = words + at + 1;
			uint32_t operandCount = length - 1;

			switch (opcode) {
			case OP_DECORATE:
				if (operandCount >= 3 && operands[1] == DECORATION_BINDING) {
					hasBinding.insert(operands[0]);
				} else if (operandCount >= 2 && operands[1] == DECORATION_NON_WRITABLE) {
					nonWritable.insert(operands[0]);
				}
				break;
			case OP_TYPE_POINTER:
				if (operandCount >= 3) {
					pointers[operands[0]] = {operands[1], operands[2]};
				}
				break;
			case OP_VARIABLE:
				if (operandCount >= 3) {
					variables[operands[1]] = {operands[0], operands[2]};
				}
				break;
			case OP_TYPE_IMAGE:
				if (operandCount >= 7) {
					types[operands[0]].Opcode = OP_TYPE_IMAGE;
					types[operands[0]].ComponentCount = operands[6];
				}
				break;
			case OP_TYPE_SAMPLED_IMAGE:
				if (operandCount >= 2) {
					types[operands[0]].Opcode = OP_TYPE_SAMPLED_IMAGE;
					types[operands[0]].ComponentType = operands[1];
				}
				break;
			case OP_TYPE_STRUCT:
				if (operandCount >= 1) {
					types[operands[0]].Opcode = OP_TYPE_STRUCT;
				}
				break;
			default:
				break;
			}

			at += length;
		}

		for (const auto &[variableId, typeAndStorage] : variables) {
			// Only the bound resources matter; locals and builtins are not
			if (!hasBinding.count(variableId)) {
				continue;
			}

			auto pointerIt = pointers.find(typeAndStorage.first);
			if (pointerIt == pointers.end()) {
				continue;
			}

			uint32_t storageClass = pointerIt->second.first;
			uint32_t pointeeId = pointerIt->second.second;
			auto typeIt = types.find(pointeeId);
			if (typeIt == types.end()) {
				continue;
			}

			if (storageClass == STORAGE_CLASS_UNIFORM && typeIt->second.Opcode == OP_TYPE_STRUCT) {
				counts.UniformBuffers++;
			} else if (storageClass == STORAGE_CLASS_UNIFORM_CONSTANT) {
				if (typeIt->second.Opcode == OP_TYPE_SAMPLED_IMAGE) {
					counts.SampledImages++;
				} else if (typeIt->second.Opcode == OP_TYPE_IMAGE) {
					// Sampled == 1 is read through a sampler, 2 is storage
					if (typeIt->second.ComponentCount == 1) {
						counts.SampledImages++;
					} else if (nonWritable.count(variableId)) {
						counts.ReadOnlyStorageImages++;
					} else {
						counts.WriteStorageImages++;
					}
				}
			}
		}

		counts.Found = true;
		return counts;
	}

	bool BlockUsage::Reads(const std::string &name) const {
		return ReadMembers.count(name) != 0;
	}

	BlockUsage ReflectBlockUsage(const void *spirv, size_t bytes, uint32_t binding) {
		BlockUsage usage;

		if (!spirv || bytes < HEADER_WORDS * sizeof(uint32_t) || bytes % sizeof(uint32_t) != 0) {
			return usage;
		}

		const auto *words = static_cast<const uint32_t *>(spirv);
		size_t wordCount = bytes / sizeof(uint32_t);
		if (words[0] != SPIRV_MAGIC) {
			return usage;
		}

		std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> memberNames;
		std::unordered_map<uint32_t, uint32_t> variableBindings;
		std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables;
		// Only 32-bit integer constants, which is all a member index is
		std::unordered_map<uint32_t, uint32_t> constants;

		// SPIR-V puts names, decorations, types and globals ahead of the
		// function bodies, so the structure is known by the time the second
		// pass reaches the code that reads it
		for (size_t at = HEADER_WORDS; at < wordCount;) {
			uint32_t instruction = words[at];
			uint16_t opcode = (uint16_t)(instruction & 0xFFFF);
			uint16_t length = (uint16_t)(instruction >> 16);

			if (length == 0 || at + length > wordCount) {
				break;
			}

			const uint32_t *operands = words + at + 1;
			uint32_t operandCount = length - 1;

			switch (opcode) {
			case OP_MEMBER_NAME:
				if (operandCount >= 3) {
					size_t cursor = at + 3;
					memberNames[operands[0]][operands[1]] = ReadString(words, at + length, cursor);
				}
				break;
			case OP_DECORATE:
				if (operandCount >= 3 && operands[1] == DECORATION_BINDING) {
					variableBindings[operands[0]] = operands[2];
				}
				break;
			case OP_TYPE_STRUCT:
				if (operandCount >= 1) {
					structMembers[operands[0]] = std::vector<uint32_t>(operands + 1, operands + operandCount);
				}
				break;
			case OP_TYPE_POINTER:
				if (operandCount >= 3) {
					pointers[operands[0]] = {operands[1], operands[2]};
				}
				break;
			case OP_VARIABLE:
				if (operandCount >= 3) {
					variables[operands[1]] = {operands[0], operands[2]};
				}
				break;
			case OP_CONSTANT:
				// result type, result id, then the literal value
				if (operandCount >= 3) {
					constants[operands[1]] = operands[2];
				}
				break;
			default:
				break;
			}

			at += length;
		}

		// The same walk ReflectUniformBlock makes: binding alone is ambiguous,
		// since a sampler can sit at binding 0 of another set, so the storage
		// class and a struct pointee are what actually identify the block
		uint32_t blockVariable = 0;
		uint32_t blockStruct = 0;
		for (const auto &[variableId, pointerAndStorage] : variables) {
			auto bindingIt = variableBindings.find(variableId);
			if (bindingIt == variableBindings.end() || bindingIt->second != binding) {
				continue;
			}

			auto pointerIt = pointers.find(pointerAndStorage.first);
			if (pointerIt == pointers.end() || pointerIt->second.first != STORAGE_CLASS_UNIFORM) {
				continue;
			}

			if (structMembers.count(pointerIt->second.second)) {
				blockVariable = variableId;
				blockStruct = pointerIt->second.second;
				break;
			}
		}

		if (blockVariable == 0) {
			return usage;
		}

		usage.Found = true;
		const auto &names = memberNames[blockStruct];

		auto readEverything = [&]() {
			for (const auto &[index, name] : names) {
				usage.ReadMembers.insert(name);
			}
		};

		// Ids that stand for the block itself, so a copy of the pointer is
		// followed rather than losing the trail
		std::unordered_set<uint32_t> aliases{blockVariable};

		for (size_t at = HEADER_WORDS; at < wordCount;) {
			uint32_t instruction = words[at];
			uint16_t opcode = (uint16_t)(instruction & 0xFFFF);
			uint16_t length = (uint16_t)(instruction >> 16);

			if (length == 0 || at + length > wordCount) {
				break;
			}

			const uint32_t *operands = words + at + 1;
			uint32_t operandCount = length - 1;

			switch (opcode) {
			case OP_ACCESS_CHAIN:
			case OP_IN_BOUNDS_ACCESS_CHAIN: {
				// result type, result id, base, then the indices
				if (operandCount < 3 || !aliases.count(operands[2])) {
					break;
				}

				// A chain into the block with no index at all aliases the whole
				// thing rather than selecting a member
				if (operandCount < 4) {
					aliases.insert(operands[1]);
					break;
				}

				auto constantIt = constants.find(operands[3]);
				if (constantIt == constants.end()) {
					// A computed member index cannot be pinned to one member,
					// so every member has to count as read
					readEverything();
					break;
				}

				// An unnamed member cannot be matched by name either way, so
				// there is nothing to record for one
				auto nameIt = names.find(constantIt->second);
				if (nameIt != names.end()) {
					usage.ReadMembers.insert(nameIt->second);
				}
				break;
			}
			case OP_LOAD:
				// Loading the block itself takes every member with it
				if (operandCount >= 3 && aliases.count(operands[2])) {
					readEverything();
				}
				break;
			case OP_COPY_OBJECT:
				if (operandCount >= 3 && aliases.count(operands[2])) {
					aliases.insert(operands[1]);
				}
				break;
			default:
				break;
			}

			at += length;
		}

		return usage;
	}
} // namespace gargantuan::ShaderReflection

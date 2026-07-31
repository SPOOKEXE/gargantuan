#include "gargantuan/render/ShaderReflection.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_set>

namespace gargantuan::ShaderReflection {
	namespace {
		constexpr uint32_t SPIRV_MAGIC = 0x07230203;
		constexpr size_t HEADER_WORDS = 5;

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

		// Opcodes needed to trace builtin member reads.
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

		constexpr uint32_t DECORATION_LOCATION = 30;

		constexpr uint32_t STORAGE_CLASS_UNIFORM_CONSTANT = 0;
		constexpr uint32_t STORAGE_CLASS_UNIFORM = 2;

		struct SpirvTypeDecl {
			uint16_t Opcode = 0;
			uint32_t WidthInBits = 0;
			uint32_t ComponentTypeId = 0;
			uint32_t ComponentCountOrSampledMode = 0;
			uint32_t StrideBytes = 0;
		};

		// A SPIR-V binary you can walk. Validating the header and stepping the
		// instruction stream is identical in all four reflectors below; only the
		// switch differs, so that is all any of them now writes.
		struct SpirvModule {
			const uint32_t *Words = nullptr;
			size_t WordCount = 0;

			explicit operator bool() const {
				return Words != nullptr;
			}

			struct Instruction {
				uint16_t Opcode = 0;
				uint16_t LengthInWords = 0;
				size_t WordOffset = 0;
				const uint32_t *Operands = nullptr;
				uint32_t OperandCount = 0;
			};

			// Stops at a malformed length rather than stalling or overrunning.
			template <typename Visit>
			void Walk(Visit &&visit) const {
				for (size_t at = HEADER_WORDS; at < WordCount;) {
					uint32_t instructionHeaderWord = Words[at];
					uint16_t length = (uint16_t)(instructionHeaderWord >> 16);
					if (length == 0 || at + length > WordCount) {
						break;
					}

					visit(Instruction{
						(uint16_t)(instructionHeaderWord & 0xFFFF), length, at, Words + at + 1, (uint32_t)(length - 1)
					});
					at += length;
				}
			}
		};

		SpirvModule ViewSpirvWords(const void *spirv, size_t bytes) {
			if (!spirv || bytes < HEADER_WORDS * sizeof(uint32_t) || bytes % sizeof(uint32_t) != 0) {
				return {};
			}

			const auto *words = static_cast<const uint32_t *>(spirv);
			if (words[0] != SPIRV_MAGIC) {
				return {};
			}
			return {words, bytes / sizeof(uint32_t)};
		}

		// Read a NUL-terminated, word-padded SPIR-V literal.
		std::string ReadString(const uint32_t *words, size_t endWordIndex, size_t &at) {
			std::string text;
			while (at < endWordIndex) {
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
	}

	const BlockMember *BlockLayout::FindMember(const std::string &name) const {
		auto it = Members.find(name);
		return it == Members.end() ? nullptr : &it->second;
	}

	BlockLayout ReflectUniformBlock(const void *spirv, size_t bytes, uint32_t binding) {
		BlockLayout layout;

		SpirvModule module = ViewSpirvWords(spirv, bytes);
		if (!module) {
			return layout;
		}

		// ReadString still walks raw words.
		[[maybe_unused]] const uint32_t *words = module.Words;
		[[maybe_unused]] const size_t wordCount = module.WordCount;

		std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> memberNames;
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> memberOffsets;
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> memberMatrixStridesBytes;
		std::unordered_map<uint32_t, uint32_t> variableBindings;
		std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables;
		std::unordered_map<uint32_t, SpirvTypeDecl> types;
		std::unordered_set<uint32_t> nonWritable;

		module.Walk([&](const SpirvModule::Instruction &decoded) {
			// Named locals so each switch below reads as it did when it owned
			// the loop. Not every reflector wants every field.
			const uint16_t opcode = decoded.Opcode;
			[[maybe_unused]] const uint16_t length = decoded.LengthInWords;
			[[maybe_unused]] const size_t at = decoded.WordOffset;
			[[maybe_unused]] const uint32_t *operands = decoded.Operands;
			[[maybe_unused]] const uint32_t operandCount = decoded.OperandCount;

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
						memberMatrixStridesBytes[operands[0]][operands[1]] = operands[3];
					}
				}
				break;
			}
			case OP_DECORATE: {
				if (operandCount >= 3 && operands[1] == DECORATION_BINDING) {
					variableBindings[operands[0]] = operands[2];
				} else if (operandCount >= 3 && operands[1] == DECORATION_ARRAY_STRIDE) {
					types[operands[0]].StrideBytes = operands[2];
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
				if (operandCount >= 3) {
					variables[operands[1]] = {operands[0], operands[2]};
				}
				break;
			}
			case OP_TYPE_INT:
			case OP_TYPE_FLOAT: {
				if (operandCount >= 2) {
					types[operands[0]].Opcode = opcode;
					types[operands[0]].WidthInBits = operands[1];
				}
				break;
			}
			case OP_TYPE_VECTOR:
			case OP_TYPE_MATRIX: {
				if (operandCount >= 3) {
					auto &type = types[operands[0]];
					type.Opcode = opcode;
					type.ComponentTypeId = operands[1];
					type.ComponentCountOrSampledMode = operands[2];
				}
				break;
			}
			case OP_TYPE_IMAGE: {
				if (operandCount >= 7) {
					auto &type = types[operands[0]];
					type.Opcode = OP_TYPE_IMAGE;
					// 1 means it is read through a sampler, 2 means storage
					type.ComponentCountOrSampledMode = operands[6];
				}
				break;
			}
			case OP_TYPE_SAMPLED_IMAGE: {
				if (operandCount >= 2) {
					auto &type = types[operands[0]];
					type.Opcode = OP_TYPE_SAMPLED_IMAGE;
					type.ComponentTypeId = operands[1];
				}
				break;
			}
			case OP_TYPE_ARRAY: {
				if (operandCount >= 2) {
					auto &type = types[operands[0]];
					type.Opcode = opcode;
					type.ComponentTypeId = operands[1];
				}
				break;
			}
			default:
				break;
			}
		});

		// Resolve the requested uniform binding to its struct type.
		uint32_t blockStructTypeId = 0;
		for (const auto &[variableId, pointerTypeIdAndStorageClass] : variables) {
			auto bindingIt = variableBindings.find(variableId);
			if (bindingIt == variableBindings.end() || bindingIt->second != binding) {
				continue;
			}

			auto pointerIt = pointers.find(pointerTypeIdAndStorageClass.first);
			if (pointerIt == pointers.end() || pointerIt->second.first != STORAGE_CLASS_UNIFORM) {
				continue;
			}

			if (structMembers.count(pointerIt->second.second)) {
				blockStructTypeId = pointerIt->second.second;
				break;
			}
		}

		if (blockStructTypeId == 0) {
			return layout;
		}

		// Compute the reflected member size.
		std::function<uint32_t(uint32_t, uint32_t)> ByteSizeOfType = [&](uint32_t typeId,
																		 uint32_t matrixStrideBytes) -> uint32_t {
			auto it = types.find(typeId);
			if (it == types.end()) {
				return 16;
			}

			const SpirvTypeDecl &type = it->second;
			switch (type.Opcode) {
			case OP_TYPE_INT:
			case OP_TYPE_FLOAT:
				return std::max(type.WidthInBits / 8u, 1u);
			case OP_TYPE_VECTOR:
				return ByteSizeOfType(type.ComponentTypeId, 0) * type.ComponentCountOrSampledMode;
			case OP_TYPE_MATRIX:
				// std140 pads every column out to the matrix stride
				return (matrixStrideBytes ? matrixStrideBytes : 16) * type.ComponentCountOrSampledMode;
			default:
				// Unknown types conservatively occupy one writable slot.
				return 16;
			}
		};

		const auto &members = structMembers[blockStructTypeId];
		const auto &names = memberNames[blockStructTypeId];
		const auto &offsets = memberOffsets[blockStructTypeId];
		const auto &strides = memberMatrixStridesBytes[blockStructTypeId];

		uint32_t blockEndBytes = 0;
		for (uint32_t index = 0; index < members.size(); index++) {
			auto offsetIt = offsets.find(index);
			auto nameIt = names.find(index);
			if (offsetIt == offsets.end() || nameIt == names.end()) {
				continue;
			}

			auto strideIt = strides.find(index);
			uint32_t sizeBytes = ByteSizeOfType(members[index], strideIt == strides.end() ? 0 : strideIt->second);

			layout.Members[nameIt->second] = BlockMember{offsetIt->second, sizeBytes};
			blockEndBytes = std::max(blockEndBytes, offsetIt->second + sizeBytes);
		}

		layout.SizeBytes = blockEndBytes;
		layout.WasBlockFound = !layout.Members.empty();
		// The block was located and it has members, yet not one could be named, so
		// the names were stripped rather than the block being absent. Reported
		// instead of being folded into `!WasBlockFound`, because the caller's response
		// differs: no parameters is fine, unnameable parameters is a broken build.
		layout.AreMemberNamesStripped = !members.empty() && names.empty();
		return layout;
	}

	ResourceCounts ReflectResources(const void *spirv, size_t bytes) {
		ResourceCounts counts;

		SpirvModule module = ViewSpirvWords(spirv, bytes);
		if (!module) {
			return counts;
		}

		// ReadString still walks raw words.
		[[maybe_unused]] const uint32_t *words = module.Words;
		[[maybe_unused]] const size_t wordCount = module.WordCount;

		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables;
		std::unordered_map<uint32_t, SpirvTypeDecl> types;
		std::unordered_set<uint32_t> nonWritable;
		std::unordered_set<uint32_t> boundVariableIds;

		module.Walk([&](const SpirvModule::Instruction &decoded) {
			// Named locals so each switch below reads as it did when it owned
			// the loop. Not every reflector wants every field.
			const uint16_t opcode = decoded.Opcode;
			[[maybe_unused]] const uint16_t length = decoded.LengthInWords;
			[[maybe_unused]] const size_t at = decoded.WordOffset;
			[[maybe_unused]] const uint32_t *operands = decoded.Operands;
			[[maybe_unused]] const uint32_t operandCount = decoded.OperandCount;

			switch (opcode) {
			case OP_DECORATE:
				if (operandCount >= 3 && operands[1] == DECORATION_BINDING) {
					boundVariableIds.insert(operands[0]);
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
					types[operands[0]].ComponentCountOrSampledMode = operands[6];
				}
				break;
			case OP_TYPE_SAMPLED_IMAGE:
				if (operandCount >= 2) {
					types[operands[0]].Opcode = OP_TYPE_SAMPLED_IMAGE;
					types[operands[0]].ComponentTypeId = operands[1];
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
		});

		for (const auto &[variableId, pointerTypeIdAndStorageClass] : variables) {
			if (!boundVariableIds.count(variableId)) {
				continue;
			}

			auto pointerIt = pointers.find(pointerTypeIdAndStorageClass.first);
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
					if (typeIt->second.ComponentCountOrSampledMode == 1) {
						counts.SampledImages++;
					} else if (nonWritable.count(variableId)) {
						counts.ReadOnlyStorageImages++;
					} else {
						counts.ReadWriteStorageImages++;
					}
				}
			}
		}

		counts.WasSpirvParsed = true;
		return counts;
	}

	bool BlockUsage::MayRead(const std::string &name) const {
		// Nothing can be named, so nothing can be ruled out. This is what the
		// unresolved-access path already does within a readable module; without it
		// a stripped module reports every member unread and a changed parameter
		// never invalidates anything.
		return AreMemberNamesStripped || ReadMembers.count(name) != 0;
	}

	BlockUsage ReflectBlockUsage(const void *spirv, size_t bytes, uint32_t binding) {
		BlockUsage usage;

		SpirvModule module = ViewSpirvWords(spirv, bytes);
		if (!module) {
			return usage;
		}

		// ReadString still walks raw words.
		[[maybe_unused]] const uint32_t *words = module.Words;
		[[maybe_unused]] const size_t wordCount = module.WordCount;

		std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> memberNames;
		std::unordered_map<uint32_t, uint32_t> variableBindings;
		std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;
		std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> variables;
		std::unordered_map<uint32_t, uint32_t> constants;

		// First pass records declarations; the second follows reads.
		module.Walk([&](const SpirvModule::Instruction &decoded) {
			// Named locals so each switch below reads as it did when it owned
			// the loop. Not every reflector wants every field.
			const uint16_t opcode = decoded.Opcode;
			[[maybe_unused]] const uint16_t length = decoded.LengthInWords;
			[[maybe_unused]] const size_t at = decoded.WordOffset;
			[[maybe_unused]] const uint32_t *operands = decoded.Operands;
			[[maybe_unused]] const uint32_t operandCount = decoded.OperandCount;

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
				if (operandCount >= 3) {
					constants[operands[1]] = operands[2];
				}
				break;
			default:
				break;
			}
		});

		// Identify the block by binding, uniform storage class, and struct pointee.
		uint32_t blockVariableId = 0;
		uint32_t blockStructTypeId = 0;
		for (const auto &[variableId, pointerTypeIdAndStorageClass] : variables) {
			auto bindingIt = variableBindings.find(variableId);
			if (bindingIt == variableBindings.end() || bindingIt->second != binding) {
				continue;
			}

			auto pointerIt = pointers.find(pointerTypeIdAndStorageClass.first);
			if (pointerIt == pointers.end() || pointerIt->second.first != STORAGE_CLASS_UNIFORM) {
				continue;
			}

			if (structMembers.count(pointerIt->second.second)) {
				blockVariableId = variableId;
				blockStructTypeId = pointerIt->second.second;
				break;
			}
		}

		if (blockVariableId == 0) {
			return usage;
		}

		usage.WasBlockFound = true;
		const auto &names = memberNames[blockStructTypeId];
		usage.AreMemberNamesStripped = !structMembers[blockStructTypeId].empty() && names.empty();

		auto MarkAllMembersRead = [&]() {
			for (const auto &[index, name] : names) {
				usage.ReadMembers.insert(name);
			}
		};

		// Track block-pointer aliases through copies.
		std::unordered_set<uint32_t> blockPointerIds{blockVariableId};

		module.Walk([&](const SpirvModule::Instruction &decoded) {
			// Named locals so each switch below reads as it did when it owned
			// the loop. Not every reflector wants every field.
			const uint16_t opcode = decoded.Opcode;
			[[maybe_unused]] const uint16_t length = decoded.LengthInWords;
			[[maybe_unused]] const size_t at = decoded.WordOffset;
			[[maybe_unused]] const uint32_t *operands = decoded.Operands;
			[[maybe_unused]] const uint32_t operandCount = decoded.OperandCount;

			switch (opcode) {
			case OP_ACCESS_CHAIN:
			case OP_IN_BOUNDS_ACCESS_CHAIN: {
				if (operandCount < 3 || !blockPointerIds.count(operands[2])) {
					break;
				}

				// An unindexed chain aliases the whole block.
				if (operandCount < 4) {
					blockPointerIds.insert(operands[1]);
					break;
				}

				auto constantIt = constants.find(operands[3]);
				if (constantIt == constants.end()) {
					// Computed indices conservatively read every member.
					MarkAllMembersRead();
					break;
				}

				// Unnamed members cannot be reported by name.
				auto nameIt = names.find(constantIt->second);
				if (nameIt != names.end()) {
					usage.ReadMembers.insert(nameIt->second);
				}
				break;
			}
			case OP_LOAD:
				// Loading the block itself takes every member with it
				if (operandCount >= 3 && blockPointerIds.count(operands[2])) {
					MarkAllMembersRead();
				}
				break;
			case OP_COPY_OBJECT:
				if (operandCount >= 3 && blockPointerIds.count(operands[2])) {
					blockPointerIds.insert(operands[1]);
				}
				break;
			default:
				break;
			}
		});

		return usage;
	}
}

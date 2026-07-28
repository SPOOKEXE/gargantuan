#include "gargantuan/reflection/TypedefGenerator.hpp"
#include "gargantuan/ClassRegistry.hpp"
#include "gargantuan/datatypes/CFrame.hpp"
#include "gargantuan/datatypes/Color3.hpp"
#include "gargantuan/datatypes/Color4.hpp"
#include "gargantuan/datatypes/Enum.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/PhysicalProperties.hpp"
#include "gargantuan/datatypes/Random.hpp"
#include "gargantuan/datatypes/Signal.hpp"
#include "gargantuan/datatypes/TweenInfo.hpp"
#include "gargantuan/datatypes/UDim.hpp"
#include "gargantuan/datatypes/UDim2.hpp"
#include "gargantuan/datatypes/Vector2.hpp"
#include "gargantuan/reflection/Enums.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace gargantuan::TypedefGenerator {
	// Metamethods are not addressable from Luau source, so they are skipped
	static bool IsMetamethod(std::string_view name) {
		return name.starts_with("__");
	}

	// Enums reflect as "Enum.PlaybackState", which reads well in runtime error
	// messages but is not a type Luau can resolve -- a dotted name off a
	// declared variable is not a type. Each enum gets a real class instead.
	static std::string EnumTypeName(std::string_view enumName) {
		return "Enum_" + std::string(enumName);
	}

	static std::string NormaliseType(std::string type) {
		const std::string prefix = "Enum.";
		for (size_t at = type.find(prefix); at != std::string::npos; at = type.find(prefix, at + 5)) {
			type.replace(at, prefix.size(), "Enum_");
		}
		return type;
	}

	// Each userdata type has its own Property type, so this stays generic
	template <typename Property> static std::string DescribeProperty(const Property &property) {
		return property.ReflectType ? NormaliseType(property.ReflectType()) : "any";
	}

	// Sorted so that regenerating the file produces identical output; the
	// underlying tables are unordered_maps
	template <typename Map> static std::vector<std::string_view> SortedKeys(const Map &map) {
		std::vector<std::string_view> keys;
		keys.reserve(map.size());
		for (const auto &[name, _] : map) {
			keys.push_back(name);
		}
		std::sort(keys.begin(), keys.end());
		return keys;
	}

	// Emits the body shared by Instance classes and standalone datatypes
	template <typename Properties, typename Methods>
	static void WriteMembers(std::ostringstream &out, const Properties &properties, const Methods &methods) {
		for (auto name : SortedKeys(properties)) {
			const auto &property = properties.at(name);
			out << "\t" << name << ": " << DescribeProperty(property) << "\n";
		}

		for (auto name : SortedKeys(methods)) {
			if (IsMetamethod(name)) {
				continue;
			}

			const auto &method = methods.at(name);
			std::string signature =
				method.ReflectType ? NormaliseType(method.ReflectType()) : "(self, ...: any): ...any";
			out << "\tfunction " << name << signature << "\n";
		}
	}

	static void WriteEnums(std::ostringstream &out) {
		out << "-- Enums\n\n";
		out << "declare class EnumItem\n";
		out << "\tName: string\n";
		out << "\tValue: number\n";
		out << "\tEnumType: Enum\n";
		out << "end\n\n";
		out << "declare class Enum\n";
		out << "\tfunction GetEnumItems(self): { EnumItem }\n";
		out << "\tfunction FromName(self, name: string): EnumItem?\n";
		out << "\tfunction FromValue(self, value: number): EnumItem?\n";
		out << "end\n\n";

		auto &enums = Enums::GetEnums();

		std::vector<std::string_view> names;
		names.reserve(enums.size());
		for (const auto &[name, _] : enums) {
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());

		// One class per enum, so that an EnumItem of one enum is not silently
		// interchangeable with an EnumItem of another
		for (auto name : names) {
			out << "declare class " << EnumTypeName(name) << " extends EnumItem\n";
			out << "end\n";
		}
		out << "\n";

		out << "declare Enum: {\n";
		for (auto name : names) {
			const auto &definition = enums.at(name);
			out << "\t" << name << ": Enum & {\n";

			for (const auto &item : definition->Items) {
				out << "\t\t" << item.Name << ": " << EnumTypeName(name) << ",\n";
			}

			out << "\t},\n";
		}
		out << "}\n\n";
	}

	static void WriteClasses(std::ostringstream &out) {
		out << "-- Instance classes\n\n";

		auto &definitions = ClassRegistry::GetDefinitionsMap();

		std::vector<const Instance::ClassDefinition *> classes;
		classes.reserve(definitions.size());
		for (const auto &[_, definition] : definitions) {
			classes.push_back(&definition);
		}

		// A class must be declared after the class it extends, so emit them in
		// depth order, then alphabetically within a depth
		auto depthOf = [](const Instance::ClassDefinition *definition) {
			int depth = 0;
			auto current = definition;
			while (current && current->Superclass.has_value()) {
				current = ClassRegistry::GetDefinitionByName(current->Superclass.value());
				depth++;
			}
			return depth;
		};

		std::sort(classes.begin(), classes.end(), [&](auto *left, auto *right) {
			int leftDepth = depthOf(left);
			int rightDepth = depthOf(right);
			if (leftDepth != rightDepth) {
				return leftDepth < rightDepth;
			}
			return left->Name < right->Name;
		});

		for (const auto *definition : classes) {
			out << "declare class " << definition->Name;
			if (definition->Superclass.has_value()) {
				out << " extends " << definition->Superclass.value();
			}
			out << "\n";

			WriteMembers(out, definition->Properties, definition->Methods);
			out << "end\n\n";
		}
	}

	// Datatypes live outside the ClassRegistry, so each one is named explicitly
	template <typename Datatype> static void WriteDatatype(std::ostringstream &out, std::string_view name) {
		out << "declare class " << name << "\n";
		WriteMembers(out, Datatype::GetUserdataProperties(), Datatype::GetUserdataMethods());
		out << "end\n\n";
	}

	static void WriteDatatypes(std::ostringstream &out) {
		out << "-- Datatypes\n\n";

		WriteDatatype<CFrame>(out, "CFrame");
		WriteDatatype<Color3>(out, "Color3");
		WriteDatatype<Color4>(out, "Color4");
		WriteDatatype<PhysicalProperties>(out, "PhysicalProperties");
		WriteDatatype<Random>(out, "Random");
		WriteDatatype<TweenInfo>(out, "TweenInfo");
		WriteDatatype<UDim>(out, "UDim");
		WriteDatatype<UDim2>(out, "UDim2");
		WriteDatatype<Vector2>(out, "Vector2");

		// Signals are generic over their arguments, which the reflection tables
		// cannot express, so they are declared by hand
		out << "declare class SignalConnection\n";
		out << "\tConnected: boolean\n";
		out << "\tfunction Disconnect(self): ()\n";
		out << "end\n\n";
		out << "declare class Signal\n";
		out << "\tfunction Connect(self, callback: (...any) -> ()): SignalConnection\n";
		out << "\tfunction Once(self, callback: (...any) -> ()): SignalConnection\n";
		out << "\tfunction Wait(self): ...any\n";
		out << "\tfunction Fire(self, ...: any): ()\n";
		out << "end\n\n";
	}

	// The constructor libraries are plain luaL_Reg tables with no reflected
	// types, so their shapes are spelled out here
	static void WriteGlobals(std::ostringstream &out) {
		out << "-- Global libraries\n\n";

		out << "declare Instance: {\n";
		out << "\tnew: (className: string) -> Instance,\n";
		out << "}\n\n";

		out << "declare Vector2: {\n";
		out << "\tnew: (x: number?, y: number?) -> Vector2,\n";
		out << "\tzero: Vector2,\n";
		out << "\tone: Vector2,\n";
		out << "\txAxis: Vector2,\n";
		out << "\tyAxis: Vector2,\n";
		out << "}\n\n";

		out << "declare Vector3: {\n";
		out << "\tnew: (x: number?, y: number?, z: number?) -> Vector3,\n";
		out << "\tzero: Vector3,\n";
		out << "\tone: Vector3,\n";
		out << "\txAxis: Vector3,\n";
		out << "\tyAxis: Vector3,\n";
		out << "\tzAxis: Vector3,\n";
		out << "}\n\n";

		out << "declare CFrame: {\n";
		out << "\tnew: (() -> CFrame) & ((position: Vector3) -> CFrame) & ((position: Vector3, lookAt: Vector3) -> "
			   "CFrame) & ((x: number, y: number, z: number) -> CFrame),\n";
		out << "\tAngles: (rx: number, ry: number, rz: number) -> CFrame,\n";
		out << "\tfromEulerAngles: (rx: number, ry: number, rz: number) -> CFrame,\n";
		out << "\tfromEulerAnglesXYZ: (rx: number, ry: number, rz: number) -> CFrame,\n";
		out << "\tfromEulerAnglesYXZ: (rx: number, ry: number, rz: number) -> CFrame,\n";
		out << "\tfromOrientation: (rx: number, ry: number, rz: number) -> CFrame,\n";
		out << "\tfromAxisAngle: (axis: Vector3, angle: number) -> CFrame,\n";
		out << "\tfromMatrix: (position: Vector3, right: Vector3, up: Vector3, back: Vector3?) -> CFrame,\n";
		out << "\tlookAt: (at: Vector3, target: Vector3, up: Vector3?) -> CFrame,\n";
		out << "\tidentity: CFrame,\n";
		out << "}\n\n";

		out << "declare Color3: {\n";
		out << "\tnew: (r: number?, g: number?, b: number?) -> Color3,\n";
		out << "\tfromRGB: (r: number, g: number, b: number) -> Color3,\n";
		out << "\tfromHSV: (h: number, s: number, v: number) -> Color3,\n";
		out << "\tfromHex: (hex: string) -> Color3,\n";
		out << "}\n\n";

		out << "declare Color4: {\n";
		out << "\tnew: ((r: number?, g: number?, b: number?, a: number?) -> Color4) & ((color: Color3, a: number?) -> "
			   "Color4),\n";
		out << "\tfromRGB: (r: number, g: number, b: number, a: number?) -> Color4,\n";
		out << "\tfromHSV: (h: number, s: number, v: number, a: number?) -> Color4,\n";
		out << "\tfromHex: (hex: string) -> Color4,\n";
		out << "\tfromColor3: (color: Color3, a: number?) -> Color4,\n";
		out << "}\n\n";

		out << "declare UDim: {\n";
		out << "\tnew: (scale: number?, offset: number?) -> UDim,\n";
		out << "}\n\n";

		out << "declare UDim2: {\n";
		out << "\tnew: ((xScale: number?, xOffset: number?, yScale: number?, yOffset: number?) -> UDim2) & ((x: UDim, "
			   "y: UDim) -> UDim2),\n";
		out << "\tfromScale: (x: number, y: number) -> UDim2,\n";
		out << "\tfromOffset: (x: number, y: number) -> UDim2,\n";
		out << "}\n\n";

		out << "declare TweenInfo: {\n";
		out << "\tnew: (time: number?, easingStyle: EnumItem?, easingDirection: EnumItem?, repeatCount: number?, "
			   "reverses: boolean?, delayTime: number?) -> TweenInfo,\n";
		out << "}\n\n";

		out << "declare Random: {\n";
		out << "\tnew: (seed: number?) -> Random,\n";
		out << "}\n\n";

		out << "declare PhysicalProperties: {\n";
		out << "\tnew: ((density: number, friction: number, elasticity: number, frictionWeight: number?, "
			   "elasticityWeight: number?) -> PhysicalProperties) & ((material: EnumItem) -> PhysicalProperties),\n";
		out << "}\n\n";

		out << "declare Signal: {\n";
		out << "\tnew: () -> Signal,\n";
		out << "}\n\n";

		out << "declare game: DataModel\n";
	}

	std::string Generate() {
		std::ostringstream out;

		out << "--!strict\n";
		out << "-- Generated by Gargantuan. Do not edit by hand.\n";
		out << "--\n";
		out << "-- Regenerate with: gargantuan --typedefs <path>\n\n";

		// Vector3 is Luau's native vector, so it has no reflection tables
		out << "-- Vector3 is Luau's built-in vector type\n";
		out << "declare class Vector3\n";
		out << "\tX: number\n";
		out << "\tY: number\n";
		out << "\tZ: number\n";
		out << "\tMagnitude: number\n";
		out << "\tUnit: Vector3\n";
		out << "end\n\n";

		WriteEnums(out);
		WriteDatatypes(out);
		WriteClasses(out);
		WriteGlobals(out);

		return out.str();
	}

	bool WriteToFile(std::string_view path) {
		std::ofstream file{std::string(path)};
		if (!file) {
			return false;
		}

		file << Generate();
		return file.good();
	}
}

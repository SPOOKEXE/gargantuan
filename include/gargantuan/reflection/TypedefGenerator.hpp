#pragma once

#include <string>
#include <string_view>

namespace gargantuan::TypedefGenerator {
	// Renders a Luau definition file describing everything the engine exposes:
	// every registered Enum, every Instance class in the ClassRegistry, every
	// userdata datatype, and the global constructor libraries.
	std::string Generate();

	// Writes Generate() to `path`, returning false if the file cannot be opened
	bool WriteToFile(std::string_view path);
}

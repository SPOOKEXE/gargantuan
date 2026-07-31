#pragma once

#include <string>
#include <string_view>

namespace gargantuan::TypedefGenerator {
	std::string Generate();

	bool WriteToFile(std::string_view path);
}

#pragma once

#include "gargantuan/datatypes/Instance.hpp"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace gargantuan {
	class ServiceProvider : public Instance {
	  public:
		typedef std::unordered_map<std::string, std::function<Instance::Pointer()>> ServiceConstructors;

		static const ClassDefinition DEFINITION;
		std::unordered_map<std::string, Instance::Pointer> Services;

		virtual Instance::Pointer FindService(std::string_view name);
		virtual Instance::Pointer GetService(std::string_view name);
		virtual const ServiceConstructors &GetServiceConstructors() const = 0;
	};
} // namespace gargantuan

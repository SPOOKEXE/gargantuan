#pragma once

#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Signal.hpp"

#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	class ServiceProvider : public Instance {
	  public:
		typedef std::unordered_map<std::string, std::function<Instance::Pointer()>> ServiceConstructors;

		static const ClassDefinition DEFINITION;
		std::unordered_map<std::string, Instance::Pointer> Services;

		virtual Instance::Pointer FindService(std::string_view name);
		virtual Instance::Pointer GetService(std::string_view name);
		virtual const ServiceConstructors &GetServiceConstructors() const = 0;

		// Every service that has been instantiated so far, in no particular order
		std::vector<Instance::Pointer> GetServices();
		// Tears down every live service; the provider is unusable afterwards
		void Close();

		G_SIGNAL(ServiceAdded, Instance::Pointer);
		G_SIGNAL(ServiceRemoving, Instance::Pointer);
		G_SIGNAL(Closing, std::monostate);
	};
} // namespace gargantuan

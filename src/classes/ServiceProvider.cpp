#include "gargantuan/classes/ServiceProvider.hpp"
#include <SDL3/SDL_log.h>
#include <stdexcept>
#include <string_view>

namespace gargantuan {
	const ServiceProvider::ClassDefinition ServiceProvider::DEFINITION = {
		.Name = "ServiceProvider",
		.Superclass = "Instance",
		.Properties =
			{
				G_UD_READONLY_PROP(ServiceProvider, ServiceAdded, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(ServiceProvider, ServiceRemoving, Signal<Instance::Pointer>::Pointer),
				G_UD_READONLY_PROP(ServiceProvider, Closing, Signal<std::monostate>::Pointer),
			},
		.Methods = {
			{"FindService", Method::Wrap<&ServiceProvider::FindService>()},
			{"GetService", Method::Wrap<&ServiceProvider::GetService>()},
			{"GetServices", Method::Wrap<&ServiceProvider::GetServices>()},
			{"Close", Method::Wrap<&ServiceProvider::Close>()},
		}
	};

	std::vector<Instance::Pointer> ServiceProvider::GetServices() {
		std::vector<Instance::Pointer> result;
		result.reserve(Services.size());
		for (const auto &[_, service] : Services) {
			result.push_back(service);
		}
		return result;
	}

	void ServiceProvider::Close() {
		Closing->Fire({});

		// Destroy detaches each service, so drain the map rather than iterate it
		auto services = std::move(Services);
		Services.clear();

		for (auto &[_, service] : services) {
			ServiceRemoving->Fire(service);
			service->Destroy();
		}
	}

	Instance::Pointer ServiceProvider::FindService(std::string_view name) {
		auto it = Services.find(std::string(name));
		if (it != Services.end()) {
			return it->second;
		}
		return nullptr;
	}

	Instance::Pointer ServiceProvider::GetService(std::string_view nameView) {
		auto name = std::string(nameView);
		auto it = Services.find(name);
		if (it == Services.end()) {
			const ServiceConstructors &constructors = GetServiceConstructors();
			if (auto constructor = constructors.find(name); constructor != constructors.end()) {
				if (!constructor->second) {
					throw std::runtime_error("Missing constructor for service " + std::string(name));
				}
				auto service = constructor->second();
				// NOTE: the constructor already names the service after its
				// class; assigning `name` here would leave Name as a
				// string_view onto this function's local string
				service->SetParent(this->shared_from_this());
				Services.emplace(name, service);
				ServiceAdded->Fire(service);
				return service;
			} else {
				throw std::runtime_error("Unknown service");
			}
		}
		return it->second;
	}
} // namespace gargantuan

#pragma once

#include "gargantuan/classes/ServiceProvider.hpp"

namespace gargantuan {
	class DataModel : public ServiceProvider {
	  public:
		static const ClassDefinition DEFINITION;
		const ServiceConstructors &GetServiceConstructors() const override;
	};
}

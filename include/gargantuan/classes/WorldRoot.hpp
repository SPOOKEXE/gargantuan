#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/datatypes/Instance.hpp"

#include <memory>
#include <vector>

namespace gargantuan {
	class WorldRoot : public Instance {
	  public:
		static const ClassDefinition DEFINITION;
		std::vector<std::shared_ptr<BasePart>> Parts;
		std::vector<BasePart *> RawParts;
		std::vector<Instance *> DirtyParts;

		WorldRoot();
		~WorldRoot();
	};
} // namespace gargantuan

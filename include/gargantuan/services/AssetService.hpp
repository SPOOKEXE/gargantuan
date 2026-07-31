#pragma once

#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"

#include <memory>

namespace gargantuan {
	class AssetService : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		std::shared_ptr<EditableImage> CreateEditableImage(Vector2 size);
	};
}

#pragma once

#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/datatypes/Vector2.hpp"

#include <memory>

namespace gargantuan {
	// Where assets are made rather than found. Roblox's AssetService also
	// fetches from its catalogue, which a standalone engine has no equivalent
	// of, so only the making half exists here.
	class AssetService : public Instance {
	  public:
		static const ClassDefinition DEFINITION;

		// A blank image of this size, transparent everywhere. Instance.new
		// would give the same thing at zero by zero; this is the spelling that
		// says how big without a separate Resize.
		//
		// The size is clamped the way Resize clamps it, so asking for something
		// silly gives a bounded image rather than a bad allocation.
		std::shared_ptr<EditableImage> CreateEditableImage(Vector2 size);
	};
} // namespace gargantuan

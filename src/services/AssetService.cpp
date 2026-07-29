#include "gargantuan/services/AssetService.hpp"
#include "gargantuan/scripting/Userdata.hpp"

namespace gargantuan {
	const AssetService::ClassDefinition AssetService::DEFINITION = {
		.Name = "AssetService",
		.Superclass = "Instance",
		.Constructor = ClassDefinition::WrapConstructor<AssetService>(),
		.Methods = {
			{"CreateEditableImage", Method::Wrap<&AssetService::CreateEditableImage>()},
		}
	};

	std::shared_ptr<EditableImage> AssetService::CreateEditableImage(Vector2 size) {
		auto image = std::make_shared<EditableImage>();
		image->Name = EditableImage::DEFINITION.Name;
		image->Resize(size);
		return image;
	}
} // namespace gargantuan

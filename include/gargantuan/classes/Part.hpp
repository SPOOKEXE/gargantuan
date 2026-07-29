#pragma once

#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/datatypes/Instance.hpp"
#include "gargantuan/reflection/Enums.hpp"

namespace gargantuan {
	G_ENUM(
		PartType,

		Ball,
		Block,
		Cylinder,
		Wedge,
		CornerWedge
	)

	class Part : public BasePart {
	  public:
		static const ClassDefinition DEFINITION;

		Enums::PartType GetShape() const {
			return Shape;
		}
		void SetShape(Enums::PartType shape) {
			Shape = shape;
			// Offset by one, so zero stays free to mean "ask GetMesh"
			MeshId = (uint8_t)((size_t)shape + 1);
		}

		Part() {
			SetShape(Shape);
		}

		std::unique_ptr<GpuMesh> &GetMesh() const override;

	  private:
		Enums::PartType Shape = Enums::PartType::Block;
	};
} // namespace gargantuan

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

		Enums::PartType Shape = Enums::PartType::Block;
		std::unique_ptr<GpuMesh> &GetMesh() const override;
	};
} // namespace gargantuan

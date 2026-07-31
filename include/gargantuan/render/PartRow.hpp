#pragma once

#include "gargantuan/render/GpuMesh.hpp"

#include <glm/glm.hpp>
#include <memory>

namespace gargantuan {
	// The part rows are stored as separate columns rather than one struct per
	// part, because the two passes do not read the same things.
	//
	//   shadow pass: model matrix + mesh + cast flag        (80 B a part)
	//   opaque pass: model matrix + mesh + cast flag + colour (96 B a part)
	//
	// Interleaved, the shadow pass pulled a colour it never looks at into cache
	// for every part it drew. Split, that colour is in a different array and
	// the shadow loop simply never touches those lines.
	//
	// Slot points at the MeshProvider's stable slot rather than the mesh, so a
	// re-upload does not leave stale pointers behind in the rows.
	struct PartMeshRow {
		std::unique_ptr<GpuMesh> *Slot = nullptr;
		bool CastShadow = true;
	};
} // namespace gargantuan

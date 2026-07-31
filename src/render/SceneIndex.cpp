#include "gargantuan/render/SceneIndex.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/WorldRoot.hpp"
#include "gargantuan/render/Frustum.hpp"
#include "gargantuan/render/SceneHash.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace gargantuan {
	namespace {
		// Everything an instance holds is a function of the part alone, which
		// is what lets one built row serve every camera.
		//
		//   T * R * S  =  [ r0*sx  r1*sy  r2*sz  position ], stored transposed
		//   so the constant bottom row is the one left out
		//
		// Plain floats through raw pointers: every glm operator[] and every
		// accessor is a call at -O0, including the two that reaching
		// &mat3[0][0] costs. mat3 columns are contiguous, so mat3[c][r] is
		// rotation[c * 3 + r].
		void BuildInstance(BasePart *part, const components::Surface &surface, PartInstance &instance) {
			const float *rotation = reinterpret_cast<const float *>(&part->Transform.CFrame.Rotation);
			const glm::vec3 &size = part->Transform.Size;
			const glm::vec3 &position = part->Transform.CFrame.Position;
			float *model = reinterpret_cast<float *>(instance.ModelRows);

			model[0] = rotation[0] * size.x;
			model[1] = rotation[3] * size.y;
			model[2] = rotation[6] * size.z;
			model[3] = position.x;
			model[4] = rotation[1] * size.x;
			model[5] = rotation[4] * size.y;
			model[6] = rotation[7] * size.z;
			model[7] = position.y;
			model[8] = rotation[2] * size.x;
			model[9] = rotation[5] * size.y;
			model[10] = rotation[8] * size.z;
			model[11] = position.z;

			const Color3 &colour = part->Visual.Color;
			instance.Color.x = colour.R;
			instance.Color.y = colour.G;
			instance.Color.z = colour.B;
			instance.Color.w = 1.0f - part->Visual.Transparency;

			// The transform the vertex stage puts the mesh's normals through,
			// not the correct inverse transpose. Wrong the same way on both
			// sides, which is all a match needs.
			glm::vec4 match = BasePart::SurfaceMatchOf(surface.Face);
			float nx = match.x, ny = match.y, nz = match.z;
			if (nx * nx + ny * ny + nz * nz > 0.0f) {
				float wx = rotation[0] * nx + rotation[3] * ny + rotation[6] * nz;
				float wy = rotation[1] * nx + rotation[4] * ny + rotation[7] * nz;
				float wz = rotation[2] * nx + rotation[5] * ny + rotation[8] * nz;
				float length = std::sqrt(wx * wx + wy * wy + wz * wz);
				if (length > 0.0f) {
					nx = wx / length;
					ny = wy / length;
					nz = wz / length;
				}
			}

			instance.SurfaceNormalAndRule.x = nx;
			instance.SurfaceNormalAndRule.y = ny;
			instance.SurfaceNormalAndRule.z = nz;
			instance.SurfaceNormalAndRule.w = match.w;
			instance.SurfaceTilingOffset.x = surface.Tiling.Value.x;
			instance.SurfaceTilingOffset.y = surface.Tiling.Value.y;
			instance.SurfaceTilingOffset.z = surface.Offset.Value.x;
			instance.SurfaceTilingOffset.w = surface.Offset.Value.y;
		}
	} // namespace

	namespace {
		// Zero means there is no key and the pass must ask the part itself,
		// which a real mesh id can never produce since those start at one
		uint16_t DrawKeyOf(const BasePart *part) {
			uint32_t meshId = part->Visual.MeshId;
			if (meshId == 0 || meshId >= MAX_MESH_IDS) {
				return 0;
			}
			return (uint16_t)(meshId * MAX_SURFACE_SLOTS + part->GetSurfaceOrDefault().TextureSlot);
		}

		uint32_t GridCellOf(const SceneIndex::PartGrid &grid, glm::vec3 point) {
			glm::vec3 local = (point - grid.Origin) / grid.CellSize;
			int32_t x = std::clamp((int32_t)local.x, 0, grid.CellCountsPerAxis[0] - 1);
			int32_t y = std::clamp((int32_t)local.y, 0, grid.CellCountsPerAxis[1] - 1);
			int32_t z = std::clamp((int32_t)local.z, 0, grid.CellCountsPerAxis[2] - 1);
			return (uint32_t)((z * grid.CellCountsPerAxis[1] + y) * grid.CellCountsPerAxis[0] + x);
		}
	} // namespace

	void SceneIndex::SyncPartRowsFromWorld(const std::shared_ptr<WorldRoot> &world) {
		const size_t count = world ? world->Parts.Raw().size() : 0;
		const size_t previous = PartRows.size();
		if (previous != count) {
			for (size_t index = count; index < previous; index++) {
				PartsHashSum -= PartRows[index].StaticHash;
			}
			PartRows.resize(count);
			PartInstances.resize(count);
			SurfaceRowsStale = true;
			GridStale = true;
		}
		if (!world) {
			return;
		}

		DirtyRowScratch.clear();
		world->GetRenderChannel().Drain((uint32_t)count, DirtyRowScratch);
		if (DirtyRowScratch.empty()) {
			// A world that has gone still still has cells to work back down
			if (!GridStale) {
				TightenCells();
			}
			return;
		}

		BasePart *const *rawParts = world->Parts.Raw().data();
		PartCullRow *rows = PartRows.data();
		PartInstance *instances = PartInstances.data();

		for (uint32_t rowIndex : DirtyRowScratch) {
			const size_t index = rowIndex;
			BasePart *part = rawParts[index];
			if (!part) {
				continue;
			}

			PartCullRow &row = rows[index];

			// Half-diagonal bounds every box rotation conservatively.
			const glm::vec3 &size = part->Transform.Size;
			const glm::vec3 centre = part->Transform.CFrame.Position;
			const float radius = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z) * 0.5f;

			// A part that moved inside its own cell costs nothing, one that left
			// it is taken out of that run and put on the end of another, and
			// only a cell with nowhere to put it forces the grid to be laid out
			// again. Growing or starting to cast just widens what its cell
			// claims, which is loose but never wrong.
			if (!GridStale && index < RowCells.size()) {
				uint32_t was = RowCells[index];
				uint32_t now = GridCellOf(Grid, centre);
				uint32_t cellRecord = was < Grid.CellRecordByCell.size() ? Grid.CellRecordByCell[was] : PartGrid::NO_CELL;

				if (cellRecord == PartGrid::NO_CELL) {
					GridStale = true;
				} else if (now != was) {
					if (!TryMoveRowToCell((uint32_t)index, now, part, radius, part->Visual.CastShadow)) {
						// Nowhere to put it, so the cell it is still in
						// stretches to cover where it went. That costs one cell
						// the slow road rather than costing the world a re-sort.
						Grid.CellHasShadowCaster[cellRecord] = Grid.CellHasShadowCaster[cellRecord] | (part->Visual.CastShadow ? 1 : 0);
						if (!TryStretchCell(cellRecord, centre, radius)) {
							GridStale = true;
						}
					}
				} else {
					// Stayed in its cell, which covers anything centred inside it
					// whatever the reach, so only a part that changed size or
					// stopped casting can leave the cell claiming too much
					Grid.HalfExtentsStuds[cellRecord] = std::max(Grid.HalfExtentsStuds[cellRecord], Grid.CellSize * 0.5f + radius);
					Grid.CellHasShadowCaster[cellRecord] = Grid.CellHasShadowCaster[cellRecord] | (part->Visual.CastShadow ? 1 : 0);
					if (radius != row.Radius || part->Visual.CastShadow != row.CastShadow) {
						MarkCellLoose(cellRecord);
					}
				}
			}

			row.Centre = centre;
			row.Radius = radius;
			row.CastShadow = part->Visual.CastShadow;

			// One lookup for the row and the instance below it.
			const components::Surface &surface = part->GetSurfaceOrDefault();
			Camera *camera = surface.Camera.get();
			EditableImage *image = surface.Image.get();
			if (camera != row.SurfaceCamera || image != row.SurfaceImage) {
				SurfaceRowsStale = true;
			}
			row.SurfaceCamera = camera;
			row.SurfaceImage = image;
			row.HasSurfaceCamera = camera != nullptr;
			row.HasSurface = row.HasSurfaceCamera || image != nullptr;

			// Pointer catches replacement; QuickHash catches property writes.
			uint64_t mix = 0xCBF29CE484222325ull;
			MixPointer(mix, part);
			MixBits(mix, part->QuickHash);
			PartsHashSum += mix - row.StaticHash;
			row.StaticHash = mix;

			BuildInstance(part, surface, instances[index]);
			if (!GridStale) {
				uint32_t position = DrawSlotByRow[index];
				DrawInstances[position] = instances[index];
				DrawKeys[position] = DrawKeyOf(part);
				// What the GPU copy of DrawInstances has to be told about. Slots
				// rather than rows, because the buffer is indexed by slot.
				DirtyDrawSlots.push_back(position);
			}
		}

		if (!GridStale) {
			TightenCells();
		}
	}

	void SceneIndex::RebuildGrid(const std::shared_ptr<WorldRoot> &world) {
		G_PROFILE("Grid Rebuild");
		GridStale = false;
		// Every slot now means a different part, so a list of changed slots is
		// worse than useless -- it would leave the unlisted ones holding another
		// part's transform. The next upload sends the lot.
		DrawInstancesAllDirty = true;
		DirtyDrawSlots.clear();

		const size_t count = PartRows.size();
		Grid.RowIndicesBySlot.clear();
		Grid.Starts.clear();
		Grid.MemberCounts.clear();
		Grid.Centres.clear();
		Grid.HalfExtentsStuds.clear();
		Grid.CellHasShadowCaster.clear();
		Grid.CellNeedsTighten.clear();
		Grid.LooseList.clear();
		Grid.CellRecordByCell.clear();
		Grid.LargestCellMemberCount = 0;
		RowCells.assign(count, 0);
		if (count == 0) {
			Grid.CellCountsPerAxis[0] = Grid.CellCountsPerAxis[1] = Grid.CellCountsPerAxis[2] = 1;
			return;
		}

		const PartCullRow *rows = PartRows.data();

		glm::vec3 low = rows[0].Centre;
		glm::vec3 high = rows[0].Centre;
		for (size_t index = 1; index < count; index++) {
			const glm::vec3 &centre = rows[index].Centre;
			low.x = std::min(low.x, centre.x);
			low.y = std::min(low.y, centre.y);
			low.z = std::min(low.z, centre.z);
			high.x = std::max(high.x, centre.x);
			high.y = std::max(high.y, centre.y);
			high.z = std::max(high.z, centre.z);
		}

		constexpr float TARGET_PER_CELL = 96.0f;
		constexpr int64_t MAXIMUM_CELLS = 1 << 18;

		glm::vec3 span = glm::max(high - low, glm::vec3(1.0f));
		float wanted = std::max(1.0f, (float)count / TARGET_PER_CELL);
		float cellSize = std::cbrt(span.x * span.y * span.z / wanted);

		int64_t total = 0;
		for (int attempt = 0; attempt < 8; attempt++) {
			cellSize = std::max(cellSize, 0.001f);
			total = 1;
			for (int axis = 0; axis < 3; axis++) {
				int32_t sides = (int32_t)(span[axis] / cellSize) + 1;
				Grid.CellCountsPerAxis[axis] = std::clamp(sides, 1, 1024);
				total *= Grid.CellCountsPerAxis[axis];
			}
			if (total <= MAXIMUM_CELLS) {
				break;
			}
			cellSize *= std::cbrt((float)total / (float)MAXIMUM_CELLS) * 1.05f;
		}

		Grid.Origin = low;
		Grid.CellSize = cellSize;

		std::vector<uint32_t> tally((size_t)total, 0);
		for (size_t index = 0; index < count; index++) {
			uint32_t cell = GridCellOf(Grid, rows[index].Centre);
			RowCells[index] = cell;
			tally[cell]++;
		}

		Grid.CellRecordByCell.assign((size_t)total, PartGrid::NO_CELL);
		const float half = cellSize * 0.5f;
		size_t slots = 0;

		for (size_t cell = 0; cell < (size_t)total; cell++) {
			uint32_t members = tally[cell];
			if (members == 0) {
				continue;
			}

			int32_t x = (int32_t)(cell % (size_t)Grid.CellCountsPerAxis[0]);
			int32_t y = (int32_t)((cell / (size_t)Grid.CellCountsPerAxis[0]) % (size_t)Grid.CellCountsPerAxis[1]);
			int32_t z = (int32_t)(cell / ((size_t)Grid.CellCountsPerAxis[0] * (size_t)Grid.CellCountsPerAxis[1]));

			Grid.CellRecordByCell[cell] = (uint32_t)Grid.Starts.size();
			Grid.Starts.push_back((uint32_t)slots);
			Grid.MemberCounts.push_back(members);
			Grid.Centres.push_back(low + glm::vec3((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f) * cellSize);
			// Filled in below, once the members are placed
			Grid.HalfExtentsStuds.push_back(half);
			Grid.CellHasShadowCaster.push_back(0);
			Grid.CellNeedsTighten.push_back(0);
			Grid.LargestCellMemberCount = std::max(Grid.LargestCellMemberCount, (size_t)members);

			// Exactly what it holds and no spare. A cell can still take a part
			// once one has left it, which in anything that oscillates is most
			// of them, and spare slots measured worse than they were worth:
			// they are dead weight in every scene where nothing crosses.
			slots += members;
		}
		Grid.Starts.push_back((uint32_t)slots);

		Grid.RowIndicesBySlot.resize(slots);
		{
			std::vector<uint32_t> place(Grid.Starts.begin(), Grid.Starts.end() - 1);
			for (size_t index = 0; index < count; index++) {
				Grid.RowIndicesBySlot[place[Grid.CellRecordByCell[RowCells[index]]]++] = (uint32_t)index;
			}
		}

		DrawSlotByRow.resize(count);
		DrawInstances.resize(slots);
		DrawParts.resize(slots);
		DrawKeys.resize(slots);
		DrawKeysStale = false;
		BasePart *const *sorted = world ? world->Parts.Raw().data() : nullptr;

		for (size_t cell = 0; cell < Grid.MemberCounts.size(); cell++) {
			const uint32_t start = Grid.Starts[cell];
			const uint32_t stop = start + Grid.MemberCounts[cell];
			float reach = 0.0f;
			uint8_t casts = 0;

			for (uint32_t at = start; at < stop; at++) {
				uint32_t index = Grid.RowIndicesBySlot[at];
				const PartCullRow &row = rows[index];
				reach = std::max(reach, row.Radius);
				casts |= row.CastShadow ? 1 : 0;

				DrawSlotByRow[index] = at;
				DrawInstances[at] = PartInstances[index];
				DrawParts[at] = sorted ? sorted[index] : nullptr;
				DrawKeys[at] = DrawParts[at] ? DrawKeyOf(DrawParts[at]) : 0;
			}

			Grid.HalfExtentsStuds[cell] = half + reach;
			Grid.CellHasShadowCaster[cell] = casts;
		}
	}

	void SceneIndex::MarkCellLoose(uint32_t cellRecord) {
		if (cellRecord < Grid.CellNeedsTighten.size() && Grid.CellNeedsTighten[cellRecord] == 0) {
			Grid.CellNeedsTighten[cellRecord] = 1;
			Grid.LooseList.push_back(cellRecord);
		}
	}

	void SceneIndex::TightenCells() {
		// Bounded, because a cell claiming more than it needs is only slow and
		// never wrong, so there is nothing to be gained by catching up in one go
		constexpr size_t PER_FRAME = 8;

		const float half = Grid.CellSize * 0.5f;
		const PartCullRow *rows = PartRows.data();

		for (size_t done = 0; done < PER_FRAME && !Grid.LooseList.empty(); done++) {
			uint32_t cellRecord = Grid.LooseList.back();
			Grid.LooseList.pop_back();
			Grid.CellNeedsTighten[cellRecord] = 0;

			const glm::vec3 centre = Grid.Centres[cellRecord];
			const uint32_t start = Grid.Starts[cellRecord];
			const uint32_t stop = start + Grid.MemberCounts[cellRecord];

			float need = half;
			uint8_t casts = 0;
			for (uint32_t at = start; at < stop; at++) {
				const PartCullRow &row = rows[Grid.RowIndicesBySlot[at]];
				glm::vec3 offset = glm::abs(row.Centre - centre);
				need = std::max(need, std::max(offset.x, std::max(offset.y, offset.z)) + row.Radius);
				casts |= row.CastShadow ? 1 : 0;
			}

			Grid.HalfExtentsStuds[cellRecord] = need;
			Grid.CellHasShadowCaster[cellRecord] = casts;
		}
	}

	bool SceneIndex::TryStretchCell(uint32_t cellRecord, glm::vec3 centre, float radius) {
		// The cell is a cube, so the widest axis decides
		glm::vec3 offset = glm::abs(centre - Grid.Centres[cellRecord]);
		float need = std::max(offset.x, std::max(offset.y, offset.z)) + radius;

		// Past this a cell covers most of its neighbours and stops being an
		// answer for anything
		constexpr float STRETCH_LIMIT = 2.0f;
		if (need > Grid.CellSize * STRETCH_LIMIT) {
			return false;
		}

		Grid.HalfExtentsStuds[cellRecord] = std::max(Grid.HalfExtentsStuds[cellRecord], need);
		MarkCellLoose(cellRecord);
		return true;
	}

	bool SceneIndex::TryMoveRowToCell(uint32_t index, uint32_t toCell, BasePart *part, float radius, bool castShadow) {
		if (index >= RowCells.size() || toCell >= Grid.CellRecordByCell.size()) {
			return false;
		}

		const uint32_t from = Grid.CellRecordByCell[RowCells[index]];
		const uint32_t to = Grid.CellRecordByCell[toCell];
		// Nothing was ever placed where it is headed, so that cell owns no
		// slots at all and the whole layout has to be worked out again
		if (from == PartGrid::NO_CELL || to == PartGrid::NO_CELL || Grid.MemberCounts[from] == 0) {
			return false;
		}
		if (Grid.MemberCounts[to] >= Grid.Starts[to + 1] - Grid.Starts[to]) {
			return false;
		}

		// Out of the run it was in, by pulling the last one down over it
		const uint32_t at = DrawSlotByRow[index];
		const uint32_t last = Grid.Starts[from] + Grid.MemberCounts[from] - 1;
		if (at < Grid.Starts[from] || at > last) {
			return false;
		}
		if (at != last) {
			uint32_t moved = Grid.RowIndicesBySlot[last];
			Grid.RowIndicesBySlot[at] = moved;
			DrawSlotByRow[moved] = at;
			DrawInstances[at] = DrawInstances[last];
			DrawParts[at] = DrawParts[last];
			DrawKeys[at] = DrawKeys[last];
			// A different part lives in this slot now, so the GPU copy of it is
			// wrong even though nothing about that part changed.
			DirtyDrawSlots.push_back(at);
		}
		Grid.MemberCounts[from]--;
		MarkCellLoose(from);

		// And onto the end of the run it joined. The caller writes the instance
		// once it has built it.
		const uint32_t landed = Grid.Starts[to] + Grid.MemberCounts[to]++;
		Grid.RowIndicesBySlot[landed] = index;
		DrawSlotByRow[index] = landed;
		DrawParts[landed] = part;
		DrawKeys[landed] = DrawKeyOf(part);
		RowCells[index] = toCell;

		Grid.HalfExtentsStuds[to] = std::max(Grid.HalfExtentsStuds[to], Grid.CellSize * 0.5f + radius);
		Grid.CellHasShadowCaster[to] = Grid.CellHasShadowCaster[to] | (castShadow ? 1 : 0);
		Grid.LargestCellMemberCount = std::max(Grid.LargestCellMemberCount, (size_t)Grid.MemberCounts[to]);
		return true;
	}

	void SceneIndex::EnsureDrawKeys(const std::shared_ptr<WorldRoot> &world) {
		if (world && (PartRows.size() != world->Parts.Raw().size() || !world->GetRenderChannel().Empty())) {
			SyncPartRowsFromWorld(world);
		}
		if (GridStale) {
			RebuildGrid(world);
			return;
		}
		if (!DrawKeysStale) {
			return;
		}

		G_PROFILE("Draw Keys");
		const size_t count = DrawParts.size();
		DrawKeys.resize(count);
		for (size_t at = 0; at < count; at++) {
			DrawKeys[at] = DrawParts[at] ? DrawKeyOf(DrawParts[at]) : 0;
		}
		DrawKeysStale = false;
	}

	void SceneIndex::RebuildSurfaceRows(uint64_t targetGeneration) {
		SurfaceCameras.clear();
		SurfaceCameraRows.clear();
		SurfaceImages.clear();

		uint64_t surfaces = 0x9E3779B97F4A7C15ull;
		MixBits(surfaces, targetGeneration);
		MixBits(surfaces, PartRows.size());

		for (size_t index = 0; index < PartRows.size(); index++) {
			const PartCullRow &row = PartRows[index];
			if (!row.HasSurface) {
				continue;
			}

			// Which part reads which source, so the resolved texture table is
			// thrown away when that changes
			MixBits(surfaces, index);
			MixPointer(surfaces, row.SurfaceCamera);
			MixPointer(surfaces, row.SurfaceImage);

			if (row.SurfaceCamera) {
				SurfaceCameras.push_back(row.SurfaceCamera);
				SurfaceCameraRows.push_back((uint32_t)index);
			}
			if (row.SurfaceImage) {
				SurfaceImages.push_back(row.SurfaceImage);
			}
		}

		// Sharing is the normal case, so this is where the list gets short
		std::sort(SurfaceCameras.begin(), SurfaceCameras.end());
		SurfaceCameras.erase(std::unique(SurfaceCameras.begin(), SurfaceCameras.end()), SurfaceCameras.end());
		std::sort(SurfaceImages.begin(), SurfaceImages.end());
		SurfaceImages.erase(std::unique(SurfaceImages.begin(), SurfaceImages.end()), SurfaceImages.end());

		WorldHasSurfaceCameras = !SurfaceCameras.empty();
		SurfaceSignature = surfaces;
		SurfaceRowsGeneration = targetGeneration;
		SurfaceRowsStale = false;
	}

	uint64_t SceneIndex::SyncAndComputeSceneSignature(
		const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t targetGeneration
	) {
		uint64_t hash = 0xCBF29CE484222325ull;
		MixVec3(hash, lightDirection);

		if (!world) {
			return hash;
		}

		// Count distinguishes simultaneous removal and insertion.
		MixBits(hash, world->Parts.Raw().size());

		SyncPartRowsFromWorld(world);

		// Every part's fixed share of the hash, summed as the rows were built
		MixBits(hash, PartsHashSum);

		if (SurfaceRowsStale || SurfaceRowsGeneration != targetGeneration) {
			RebuildSurfaceRows(targetGeneration);
		}

		// Lets later walks skip absent surface sources.
		WorldHasSurfaces = !SurfaceCameras.empty() || !SurfaceImages.empty();

		// Which source a part reads is fixed until the part changes, but what
		// that source holds is not, so this cannot be cached with the rows.
		for (Camera *camera : SurfaceCameras) {
			MixPointer(hash, camera);
			MixBits(hash, GetCameraDrawCount(camera));
		}
		for (EditableImage *image : SurfaceImages) {
			MixPointer(hash, image);
			MixBits(hash, image->GetRevision());
		}

		return hash;
	}

	uint64_t SceneIndex::GetCameraDrawCount(Camera *camera) const {
		if (!camera) {
			return 0;
		}

		auto it = CameraDrawCounts.find(camera);
		return it == CameraDrawCounts.end() ? 0 : it->second;
	}

	void SceneIndex::CountCameraDraw(Camera *camera) {
		if (camera) {
			CameraDrawCounts[camera]++;
		}
	}

	void SceneIndex::ComputeVisibleSet(
		Camera *camera,
		const std::shared_ptr<WorldRoot> &world,
		glm::vec3 lightDirection,
		bool needSignature,
		VisibleSet &out
	) {
		G_PROFILE("Frustum Walk");
		out.VisiblePartsHashValid = needSignature;
		out.InView.clear();
		out.InViewCount = 0;
		out.ShadowCount = 0;

		// Only surface-camera redraw checks require lookup sets.
		const bool needSets = WorldHasSurfaceCameras;
		const bool worldHasSurfaces = WorldHasSurfaces;

		if (world) {
			// Reserve once for large worlds.
			if (out.InViewList.size() < world->Parts.Raw().size()) {
				out.InViewList.resize(world->Parts.Raw().size());
				out.ShadowCasterList.resize(world->Parts.Raw().size());
				out.InViewIndexList.resize(world->Parts.Raw().size());
				out.ShadowIndexList.resize(world->Parts.Raw().size());
			}
		}

		uint64_t hash = 0xCBF29CE484222325ull;
		MixVec3(hash, lightDirection);

		if (!camera || !world) {
			out.VisiblePartsHash = hash;
			return;
		}

		WorldSidePlanes planes = ExtractSidePlanes(camera->GetProjectionMatrix() * camera->GetViewMatrix());
		// Shadows extend opposite the toward-light vector.
		glm::vec3 shadowStep = -glm::normalize(lightDirection) * SHADOW_CAST_REACH;

		uint64_t visible = 0;

		const bool measuring = G_PROFILE_COLLECTING();
		uint64_t cullNanoseconds = 0;
		uint64_t gatherNanoseconds = 0;
		uint64_t signatureNanoseconds = 0;

		const size_t total = world->Parts.Raw().size();
		// A world mutated since the signature pass, or one drawn without one.
		if (PartRows.size() != total || !world->GetRenderChannel().Empty()) {
			SyncPartRowsFromWorld(world);
		}
		if (GridStale) {
			RebuildGrid(world);
		}

		BasePart *const *rawParts = world->Parts.Raw().data();
		const PartCullRow *bounds = PartRows.data();
		BasePart **inViewOut = out.InViewList.data();
		uint32_t *inViewIndexOut = out.InViewIndexList.data();
		BasePart **shadowOut = out.ShadowCasterList.data();
		uint32_t *shadowIndexOut = out.ShadowIndexList.data();
		size_t inViewCount = 0;
		size_t shadowCount = 0;

		CullScratch.resize(std::max<size_t>(Grid.LargestCellMemberCount, 1));

		const size_t cellCount = Grid.Centres.size();
		const uint32_t *gridRows = Grid.RowIndicesBySlot.data();
		BasePart *const *drawParts = DrawParts.data();

		for (size_t cell = 0; cell < cellCount; cell++) {
			const size_t memberCount = Grid.MemberCounts[cell];
			// Emptied by everything in it moving out
			if (memberCount == 0) {
				continue;
			}

			const uint32_t cellStart = Grid.Starts[cell];
			const uint32_t *members = gridRows + cellStart;
			BasePart *const *cellParts = drawParts + cellStart;
			const bool cellCasts = Grid.CellHasShadowCaster[cell] != 0;

			uint64_t cullStart = measuring ? SDL_GetTicksNS() : 0;
			int side = ClassifyBoxAgainstPlanes(planes, Grid.Centres[cell], Grid.HalfExtentsStuds[cell]);

			const bool decided = !cellCasts && side != 0 && !needSignature;
			if (decided && side < 0) {
				if (measuring) {
					cullNanoseconds += SDL_GetTicksNS() - cullStart;
				}
				continue;
			}

			if (!decided) {
				const bool allInside = side > 0;
				const bool allOutside = side < 0;
				for (size_t at = 0; at < memberCount; at++) {
					const PartCullRow &bound = bounds[members[at]];
					CullResult &result = CullScratch[at];

					const glm::vec3 &centre = bound.Centre;
					const float radius = bound.Radius;

					result.InView = allInside || (!allOutside && SphereInside(planes, centre, radius));

					float reach = SHADOW_VOLUME_RADIUS + radius;
					bool inShadowVolume = bound.CastShadow &&
						centre.x * centre.x + centre.y * centre.y + centre.z * centre.z <= reach * reach;

					result.ShadowReaches = !result.InView && inShadowVolume &&
						CapsuleInside(planes, centre, centre + shadowStep, radius);
					result.CastsShadow = inShadowVolume && (result.InView || result.ShadowReaches);
				}
			}
			if (measuring) {
				cullNanoseconds += SDL_GetTicksNS() - cullStart;
			}

			uint64_t gatherStart = measuring ? SDL_GetTicksNS() : 0;
			if (decided) {
				for (size_t at = 0; at < memberCount; at++) {
					BasePart *part = cellParts[at];
					if (needSets) {
						out.InView.insert(part);
					}
					inViewIndexOut[inViewCount] = (uint32_t)(cellStart + at);
					inViewOut[inViewCount++] = part;
				}
			} else {
				for (size_t at = 0; at < memberCount; at++) {
					BasePart *part = cellParts[at];
					const CullResult &result = CullScratch[at];
					if (result.InView) {
						if (needSets) {
							out.InView.insert(part);
						}
						inViewIndexOut[inViewCount] = (uint32_t)(cellStart + at);
						inViewOut[inViewCount++] = part;
					}
					if (result.CastsShadow) {
						shadowIndexOut[shadowCount] = (uint32_t)(cellStart + at);
						shadowOut[shadowCount++] = part;
					}
				}
			}
			if (measuring) {
				gatherNanoseconds += SDL_GetTicksNS() - gatherStart;
			}

			if (!needSignature) {
				continue;
			}

			uint64_t signatureStart = measuring ? SDL_GetTicksNS() : 0;
			for (size_t at = 0; at < memberCount; at++) {
				BasePart *part = cellParts[at];
				const CullResult &result = CullScratch[at];
				// Hash visible parts and offscreen casters affecting view.
				if (!result.InView && !result.ShadowReaches) {
					continue;
				}

				visible++;
				MixPointer(hash, part);
				MixBits(hash, part->QuickHash);
				// QuickHash detects newly added surfaces omitted here.
				if (worldHasSurfaces) {
					const components::Surface &surface = part->GetSurfaceOrDefault();
					if (surface.Camera || surface.Image) {
						MixPointer(hash, surface.Camera.get());
						MixBits(hash, GetCameraDrawCount(surface.Camera.get()));
						MixPointer(hash, surface.Image.get());
						MixBits(hash, surface.Image ? surface.Image->GetRevision() : 0);
					}
				}
			}
			if (measuring) {
				signatureNanoseconds += SDL_GetTicksNS() - signatureStart;
			}
		}

		out.InViewCount = inViewCount;
		out.ShadowCount = shadowCount;

		if (measuring) {
			// One zone per part would be a hundred thousand of them a frame, so
			// the walk times itself and plots the totals instead
			ProfilerCountTime("Cull ms", cullNanoseconds);
			ProfilerCountTime("Gather ms", gatherNanoseconds);
			ProfilerCountTime("Signature ms", signatureNanoseconds);
			ProfilerCount("Parts HasBeenBuilt", total);
		}

		// Include visible count, especially for empty views.
		MixBits(hash, visible);
		out.VisiblePartsHash = hash;
	}

	const VisibleSet &SceneIndex::EnsureVisibleSet(
		Camera *camera,
		const std::shared_ptr<WorldRoot> &world,
		glm::vec3 lightDirection,
		uint64_t cameraSignature,
		bool needSignature
	) {
		VisibleSet &set = VisibleSets[camera];

		// Reuse one walk while scene and camera stamps match, unless it was
		// taken without the signature and this caller wants one.
		if (set.HasBeenBuilt && set.SceneStamp == SceneSignature && set.CameraStamp == cameraSignature &&
			(!needSignature || set.VisiblePartsHashValid)) {
			return set;
		}

		ComputeVisibleSet(camera, world, lightDirection, needSignature, set);
		set.SceneStamp = SceneSignature;
		set.CameraStamp = cameraSignature;
		set.HasBeenBuilt = true;
		return set;
	}

	void SceneIndex::ForgetCamera(Camera *camera) {
		VisibleSets.erase(camera);
		CameraDrawCounts.erase(camera);
	}
}

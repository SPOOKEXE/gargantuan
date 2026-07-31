#pragma once

#include "gargantuan/render/InstanceData.hpp"
#include "gargantuan/render/RenderPass.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gargantuan {
	class BasePart;
	class Camera;
	class EditableImage;

	// The CPU-side view of the scene: one row per part, a loose spatial grid
	// laid over those rows, and the per-camera visible sets culled out of it.
	// Nothing here touches the GPU -- it is what the renderer reads to decide
	// what to draw, not part of drawing it.
	class SceneIndex {
	  public:
		// A part reduced to what culling and redraw checks need, so neither has
		// to walk the instance itself.
		struct PartCullRow {
			glm::vec3 Centre{0.0f};
			float Radius = 0.0f;
			bool CastShadow = false;
			bool HasSurface = false;
			bool HasSurfaceCamera = false;
			Camera *SurfaceCamera = nullptr;
			EditableImage *SurfaceImage = nullptr;
			uint64_t StaticHash = 0;
		};

		// Cells hold runs of row indices. Half-extents are per-cell rather than
		// the cell size because a cell stretches to cover a part that walked out
		// of it, which is looser than the grid but cheaper than re-sorting.
		struct PartGrid {
			static constexpr uint32_t NO_CELL = 0xFFFFFFFFu;

			glm::vec3 Origin{0.0f};
			float CellSize = 1.0f;
			int32_t CellCountsPerAxis[3] = {1, 1, 1};

			std::vector<uint32_t> RowIndicesBySlot;
			std::vector<uint32_t> Starts;
			std::vector<uint32_t> MemberCounts;
			std::vector<glm::vec3> Centres;
			std::vector<float> HalfExtentsStuds;
			std::vector<uint8_t> CellHasShadowCaster;
			std::vector<uint8_t> CellNeedsTighten;
			std::vector<uint32_t> LooseList;
			std::vector<uint32_t> CellRecordByCell;
			size_t LargestCellMemberCount = 0;
		};

		// Stamped by the last SyncAndComputeSceneSignature; visible sets compare
		// against it to decide whether their walk still stands.
		uint64_t SceneSignature = 0;

		uint64_t SyncAndComputeSceneSignature(
			const std::shared_ptr<WorldRoot> &world, glm::vec3 lightDirection, uint64_t targetGeneration
		);

		void ComputeVisibleSet(
			Camera *camera,
			const std::shared_ptr<WorldRoot> &world,
			glm::vec3 lightDirection,
			bool needSignature,
			VisibleSet &out
		);

		const VisibleSet &EnsureVisibleSet(
			Camera *camera,
			const std::shared_ptr<WorldRoot> &world,
			glm::vec3 lightDirection,
			uint64_t cameraSignature,
			bool needSignature = false
		);

		void EnsureDrawKeys(const std::shared_ptr<WorldRoot> &world);
		void SyncPartRowsFromWorld(const std::shared_ptr<WorldRoot> &world);

		// How many times a camera has drawn, which is what makes a surface
		// reading that camera's target look stale to the redraw check.
		uint64_t GetCameraDrawCount(Camera *camera) const;
		void CountCameraDraw(Camera *camera);

		void ForgetCamera(Camera *camera);

		// Rows, in world order. PartInstances is the unsorted companion; the
		// grid rearranges copies of it into DrawInstances.
		std::vector<PartCullRow> PartRows;
		std::vector<PartInstance> PartInstances;

		// Parts that show a camera or an image, deduplicated. The renderer
		// resolves these into texture slots.
		std::vector<Camera *> SurfaceCameras;
		std::vector<EditableImage *> SurfaceImages;
		bool WorldHasSurfaceCameras = false;
		bool WorldHasSurfaces = false;
		uint64_t SurfaceSignature = 0;

		// Grid order, which is also the GPU instance-buffer order.
		PartGrid Grid;
		std::vector<PartInstance> DrawInstances;
		std::vector<uint32_t> DirtyDrawSlots;
		bool DrawInstancesAllDirty = true;
		std::vector<BasePart *> DrawParts;
		std::vector<uint16_t> DrawKeys;

		// Row indices of parts showing a camera, for the demand walk.
		std::vector<uint32_t> SurfaceCameraRows;

		// The resolved texture table is keyed off SurfaceSignature, so the
		// renderer marks keys stale when it reassigns slots.
		bool DrawKeysStale = true;

	  private:
		struct CullResult {
			bool InView = false;
			bool ShadowReaches = false;
			bool CastsShadow = false;
		};

		void RebuildGrid(const std::shared_ptr<WorldRoot> &world);
		void RebuildSurfaceRows(uint64_t targetGeneration);
		bool TryMoveRowToCell(uint32_t index, uint32_t toCell, BasePart *part, float radius, bool castShadow);
		bool TryStretchCell(uint32_t cellRecord, glm::vec3 centre, float radius);
		void MarkCellLoose(uint32_t cellRecord);
		void TightenCells();

		bool GridStale = true;
		bool SurfaceRowsStale = true;
		uint64_t SurfaceRowsGeneration = 0;
		uint64_t PartsHashSum = 0;

		std::vector<uint32_t> DirtyRowScratch;
		std::vector<uint32_t> DrawSlotByRow;
		std::vector<uint32_t> RowCells;
		std::vector<CullResult> CullScratch;

		std::unordered_map<Camera *, uint64_t> CameraDrawCounts;
		std::unordered_map<Camera *, VisibleSet> VisibleSets;
	};
}

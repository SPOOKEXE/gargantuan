// Which cameras have to draw this frame, and in what order. A camera that
// samples another has to be drawn after it, and a camera shown on a part's
// surface has to be drawn whether or not it is Enabled -- so this is a
// dependency walk, not a list.
#include "gargantuan/render/RenderProvider.hpp"

#include "gargantuan/Profiler.hpp"
#include "gargantuan/classes/BasePart.hpp"
#include "gargantuan/classes/Camera.hpp"
#include "gargantuan/classes/EditableImage.hpp"
#include "gargantuan/classes/ShaderScript.hpp"
#include "gargantuan/render/Frustum.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

namespace gargantuan {
	std::vector<Camera *> RenderProvider::GetDirectlySampledCameras(Camera *camera) {
		std::vector<Camera *> sampled;
		if (!camera) {
			return sampled;
		}

		auto collect = [&sampled](const std::shared_ptr<ShaderScript> &shader) {
			if (!shader) {
				return;
			}
			for (const auto &source : shader->GetProperties()->GetTextureSources()) {
				if (source.Camera) {
					sampled.push_back(source.Camera.get());
				}
			}
		};

		for (const auto &shader : BuildShaderChain(camera)) {
			collect(shader);
		}
		collect(camera->SurfaceShader);

		return sampled;
	}

	std::vector<Camera *> RenderProvider::GetRenderOrder(const std::vector<Camera *> &roots) {
		std::vector<Camera *> order;
		std::unordered_set<Camera *> finished;
		std::unordered_set<Camera *> visiting;

		// Post-order DFS draws every input before its reader.
		std::function<void(Camera *)> visit = [&](Camera *camera) {
			if (!camera || finished.count(camera)) {
				return;
			}

			if (visiting.count(camera)) {
				// Break sampling cycles with a prior-frame read.
				CamerasNeedingHistory.insert(camera);
				if (ReportedCycles.insert(camera).second) {
					SDL_Log(
						"Camera '%.*s' is part of a loop of cameras sampling each other; "
						"the edge that closes the loop reads its previous frame",
						(int)camera->Name.size(),
						camera->Name.data()
					);
				}
				return;
			}

			visiting.insert(camera);
			for (Camera *dependency : GetDirectlySampledCameras(camera)) {
				if (visiting.count(dependency)) {
					// Record the reader that must use prior-frame data.
					PriorFrameReaderToSourceEdges.insert({camera, dependency});
				}
				visit(dependency);
			}
			visiting.erase(camera);

			finished.insert(camera);
			order.push_back(camera);
		};

		for (Camera *root : roots) {
			visit(root);
		}

		return order;
	}

	namespace {
		glm::mat4 WidenedProjection(Camera &camera, float fieldOfViewMarginFraction) {
			if (fieldOfViewMarginFraction <= 0.0f) {
				return camera.GetProjectionMatrix();
			}

			constexpr float WIDEST_FIELD_OF_VIEW_DEGREES = 179.0f;
			float fieldOfViewDegrees =
				glm::min(camera.FieldOfView * (1.0f + fieldOfViewMarginFraction), WIDEST_FIELD_OF_VIEW_DEGREES);
			return glm::perspective(
				glm::radians(fieldOfViewDegrees), camera.GetAspectRatio(), Camera::NEAR_PLANE, Camera::FAR_PLANE
			);
		}
	} // namespace

	const std::unordered_set<Camera *> &RenderProvider::GetDemandedCameras(
		const std::vector<Camera *> &viewers, float fieldOfViewMarginFraction
	) {
		G_PROFILE("Camera Demand");

		DemandedCameras.clear();
		DemandStack.assign(viewers.begin(), viewers.end());

		while (!DemandStack.empty()) {
			Camera *camera = DemandStack.back();
			DemandStack.pop_back();
			if (!camera || !DemandedCameras.insert(camera).second) {
				continue;
			}

			for (Camera *sampled : GetDirectlySampledCameras(camera)) {
				DemandStack.push_back(sampled);
			}

			if (SceneDrawIndex.SurfaceCameraRows.empty()) {
				continue;
			}

			WorldSidePlanes planes =
				ExtractSidePlanes(WidenedProjection(*camera, fieldOfViewMarginFraction) * camera->GetViewMatrix());

			for (uint32_t partRowIndex : SceneDrawIndex.SurfaceCameraRows) {
				if (partRowIndex >= SceneDrawIndex.PartRows.size()) {
					continue;
				}

				const SceneIndex::PartCullRow &row = SceneDrawIndex.PartRows[partRowIndex];
				if (row.SurfaceCamera && SphereInside(planes, row.Centre, row.Radius)) {
					DemandStack.push_back(row.SurfaceCamera);
				}
			}
		}

		return DemandedCameras;
	}
}

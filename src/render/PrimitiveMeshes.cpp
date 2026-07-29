// Has GPU programming gone too far?

#include "gargantuan/render/PrimitiveMeshes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>

namespace gargantuan::PrimitiveMeshes {
	// NOTE: the names are (u, v). UV_00 used to carry UV_01's value, which put
	// two corners of every quad on the same texel and skewed whatever was drawn
	// on a face across the triangle between them.
	static constexpr glm::vec2 UV_00{0.0f, 0.0f};
	static constexpr glm::vec2 UV_01{0.0f, 1.0f};
	static constexpr glm::vec2 UV_10{1.0f, 0.0f};
	static constexpr glm::vec2 UV_11{1.0f, 1.0f};

	// The wedge's slope runs from the top of the back edge down to the bottom
	// of the front one, so it leans along Y and Z and not at all along X. It
	// used to carry 0.707 in X as well, which is a direction the face does not
	// point in: the slope lit as though it were turned a third of the way
	// towards the right-hand side.
	//
	// NOTE: BasePart::GetSurfaceMatch has to answer with this same vector, or
	// NormalId.Slope would be comparing against a face that is not there.
	static constexpr glm::vec3 SLOPE_NORMAL{0.0f, 0.70710678f, 0.70710678f};

	// NOTE: the UV on each face is chosen so a picture put on it reads upright
	// when looked at from outside: u runs to the viewer's right, v downwards to
	// match image space. The four side faces used to carry the top face's
	// assignment, which put u up the wall and turned every picture a quarter
	// turn. Only the UVs moved -- positions, normals and winding are untouched.
	Mesh Block() {
		return Mesh{
			std::vector<Vertex>{
				Vertex{{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, UV_11},
				Vertex{{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, UV_10},
				Vertex{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, UV_00},
				Vertex{{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, UV_01},

				Vertex{{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, UV_11},
				Vertex{{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, UV_10},
				Vertex{{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, UV_00},
				Vertex{{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, UV_01},

				Vertex{{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, UV_00},
				Vertex{{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, UV_01},
				Vertex{{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, UV_11},
				Vertex{{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, UV_10},

				Vertex{{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, UV_00},
				Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, UV_01},
				Vertex{{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, UV_11},
				Vertex{{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, UV_10},

				Vertex{{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, UV_01},
				Vertex{{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, UV_11},
				Vertex{{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, UV_10},
				Vertex{{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, UV_00},

				Vertex{{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_01},
				Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_11},
				Vertex{{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_10},
				Vertex{{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_00},
			},
			std::vector<uint32_t>{
				0,	1,	2,	0,	2,	3,	4,	5,	6,	4,	6,	7,	8,	9,	10, 8,	10, 11,
				12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
			},
		};
	};
	Mesh Wedge() {
		return Mesh{
			{
				Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, UV_00},
				Vertex{{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, UV_10},
				Vertex{{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, UV_11},
				Vertex{{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, UV_01},

				Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_00},
				Vertex{{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_10},
				Vertex{{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_11},
				Vertex{{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, UV_01},

				Vertex{{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, UV_00},
				Vertex{{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, UV_10},
				Vertex{{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, UV_01},

				Vertex{{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, UV_00},
				Vertex{{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, UV_10},
				Vertex{{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, UV_01},

				Vertex{{-0.5f, 0.5f, -0.5f}, SLOPE_NORMAL, UV_00},
				Vertex{{0.5f, 0.5f, -0.5f}, SLOPE_NORMAL, UV_10},
				Vertex{{0.5f, -0.5f, 0.5f}, SLOPE_NORMAL, UV_11},
				Vertex{{-0.5f, -0.5f, 0.5f}, SLOPE_NORMAL, UV_01},
			},
			{
				0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 8, 9, 10, 11, 13, 12, 14, 16, 15, 14, 17, 16,
			},
		};
	}

	namespace {
		// Enough that a part-sized ball reads round without the vertex count
		// mattering to anything
		constexpr int SPHERE_SEGMENTS = 24;
		constexpr int SPHERE_STACKS = 16;
		constexpr int CYLINDER_SEGMENTS = 24;

		// Everything here is built inside the same half unit box the block
		// occupies, so Size scales a ball or a cylinder the way it scales a
		// block rather than by some other factor
		constexpr float RADIUS = 0.5f;
	} // namespace

	Mesh Sphere() {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve((SPHERE_STACKS + 1) * (SPHERE_SEGMENTS + 1));

		// The seam is walked twice, once at u = 0 and once at u = 1, because a
		// single column of vertices cannot hold both ends of the picture
		for (int stack = 0; stack <= SPHERE_STACKS; stack++) {
			float v = (float)stack / (float)SPHERE_STACKS;
			// Latitude from the top down, so v = 0 is the top of the picture
			float phi = v * glm::pi<float>();
			float y = glm::cos(phi);
			float ring = glm::sin(phi);

			for (int segment = 0; segment <= SPHERE_SEGMENTS; segment++) {
				float u = (float)segment / (float)SPHERE_SEGMENTS;
				float theta = u * glm::two_pi<float>();

				// Starts at +Z and turns towards +X, so u runs to the right of
				// anyone looking at the front of the ball
				glm::vec3 normal{ring * glm::sin(theta), y, ring * glm::cos(theta)};
				vertices.push_back(Vertex{normal * RADIUS, normal, {u, v}});
			}
		}

		const int stride = SPHERE_SEGMENTS + 1;
		for (int stack = 0; stack < SPHERE_STACKS; stack++) {
			for (int segment = 0; segment < SPHERE_SEGMENTS; segment++) {
				uint32_t top = (uint32_t)(stack * stride + segment);
				uint32_t bottom = top + (uint32_t)stride;

				// Counter-clockwise seen from outside, which is what the
				// pipeline treats as front facing. The two triangles at a pole
				// come out zero area and cost nothing.
				indices.insert(indices.end(), {bottom, bottom + 1, top + 1});
				indices.insert(indices.end(), {bottom, top + 1, top});
			}
		}

		return Mesh{std::move(vertices), std::move(indices)};
	}

	Mesh Cylinder() {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// Lying along X, so the flat ends land on Right and Left the way
		// Roblox stands a cylinder. NormalId.Circumference reads the same axis.
		for (int segment = 0; segment <= CYLINDER_SEGMENTS; segment++) {
			float u = (float)segment / (float)CYLINDER_SEGMENTS;
			float theta = u * glm::two_pi<float>();
			glm::vec3 normal{0.0f, glm::cos(theta), glm::sin(theta)};

			// v = 0 at the +X end, so the picture runs down the length
			vertices.push_back(Vertex{{-RADIUS, normal.y * RADIUS, normal.z * RADIUS}, normal, {u, 1.0f}});
			vertices.push_back(Vertex{{RADIUS, normal.y * RADIUS, normal.z * RADIUS}, normal, {u, 0.0f}});
		}

		for (int segment = 0; segment < CYLINDER_SEGMENTS; segment++) {
			uint32_t low = (uint32_t)(segment * 2);
			uint32_t high = low + 1;
			uint32_t nextLow = low + 2;
			uint32_t nextHigh = low + 3;

			indices.insert(indices.end(), {low, nextLow, high});
			indices.insert(indices.end(), {nextLow, nextHigh, high});
		}

		// Each end is a fan around its own middle. The two run opposite ways
		// round so both wind counter-clockwise seen from outside their own end.
		auto addCap = [&](float x, float facing) {
			uint32_t centre = (uint32_t)vertices.size();
			vertices.push_back(Vertex{{x, 0.0f, 0.0f}, {facing, 0.0f, 0.0f}, {0.5f, 0.5f}});

			for (int segment = 0; segment <= CYLINDER_SEGMENTS; segment++) {
				float theta = ((float)segment / (float)CYLINDER_SEGMENTS) * glm::two_pi<float>();
				float y = glm::cos(theta) * RADIUS;
				float z = glm::sin(theta) * RADIUS;

				// Matches how the block lays out its own Right and Left faces:
				// looking at +X, right is -Z; looking at -X, right is +Z
				float u = 0.5f + (facing > 0.0f ? -z : z);
				vertices.push_back(Vertex{{x, y, z}, {facing, 0.0f, 0.0f}, {u, 0.5f - y}});
			}

			for (int segment = 0; segment < CYLINDER_SEGMENTS; segment++) {
				uint32_t rim = centre + 1 + (uint32_t)segment;
				if (facing > 0.0f) {
					indices.insert(indices.end(), {centre, rim, rim + 1});
				} else {
					indices.insert(indices.end(), {centre, rim + 1, rim});
				}
			}
		};

		addCap(RADIUS, 1.0f);
		addCap(-RADIUS, -1.0f);

		return Mesh{std::move(vertices), std::move(indices)};
	}
} // namespace gargantuan::PrimitiveMeshes

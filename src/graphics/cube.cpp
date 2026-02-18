#include "cube.h"

#include <glm/glm.hpp>

Mesh make_cube_mesh()
{
	Mesh mesh;
	mesh.sourcePath = "internal://cube";
	mesh.materialIndex = 0;
	mesh.transform = glm::mat4{1.0f};

	// 6 faces, 4 verts each = 24 verts, 36 indices
	// Each face has a constant normal and tangent
	struct FaceData
	{
		glm::vec3 normal;
		glm::vec4 tangent;
		glm::vec3 verts[4];	 // CCW winding
	};

	FaceData faces[] = {
		// Front (+Z)
		{{0, 0, 1},
		 {1, 0, 0, 1},
		 {{-0.5f, -0.5f, 0.5f},
		  {0.5f, -0.5f, 0.5f},
		  {0.5f, 0.5f, 0.5f},
		  {-0.5f, 0.5f, 0.5f}}},
		// Back (-Z)
		{{0, 0, -1},
		 {-1, 0, 0, 1},
		 {{0.5f, -0.5f, -0.5f},
		  {-0.5f, -0.5f, -0.5f},
		  {-0.5f, 0.5f, -0.5f},
		  {0.5f, 0.5f, -0.5f}}},
		// Left (-X)
		{{-1, 0, 0},
		 {0, 0, 1, 1},
		 {{-0.5f, -0.5f, -0.5f},
		  {-0.5f, -0.5f, 0.5f},
		  {-0.5f, 0.5f, 0.5f},
		  {-0.5f, 0.5f, -0.5f}}},
		// Right (+X)
		{{1, 0, 0},
		 {0, 0, -1, 1},
		 {{0.5f, -0.5f, 0.5f},
		  {0.5f, -0.5f, -0.5f},
		  {0.5f, 0.5f, -0.5f},
		  {0.5f, 0.5f, 0.5f}}},
		// Top (+Y)
		{{0, 1, 0},
		 {1, 0, 0, 1},
		 {{-0.5f, 0.5f, 0.5f},
		  {0.5f, 0.5f, 0.5f},
		  {0.5f, 0.5f, -0.5f},
		  {-0.5f, 0.5f, -0.5f}}},
		// Bottom (-Y)
		{{0, -1, 0},
		 {1, 0, 0, 1},
		 {{-0.5f, -0.5f, -0.5f},
		  {0.5f, -0.5f, -0.5f},
		  {0.5f, -0.5f, 0.5f},
		  {-0.5f, -0.5f, 0.5f}}},
	};

	glm::vec2 uvs[] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

	for (const auto& face : faces)
	{
		uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
		for (int i = 0; i < 4; ++i)
		{
			Vertex v{};
			v.pos = face.verts[i];
			v.normal = face.normal;
			v.uv = uvs[i];
			v.tangent = face.tangent;
			mesh.vertices.push_back(v);
		}
		mesh.indices.push_back(base + 0);
		mesh.indices.push_back(base + 1);
		mesh.indices.push_back(base + 2);
		mesh.indices.push_back(base + 2);
		mesh.indices.push_back(base + 3);
		mesh.indices.push_back(base + 0);
	}

	return mesh;
}

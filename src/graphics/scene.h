#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "light.h"
#include "material.h"
#include "mesh.h"
#include "texture.h"

// One node of an imported model's hierarchy (e.g. a glTF node).
struct ImportedNode
{
	std::string name;
	glm::mat4 localTransform{1.0f};
	int32_t parent = -1;			 // index into the same node list, -1 = root
	std::vector<uint32_t> children;	 // indices into the same node list
	std::vector<uint32_t> meshes;	 // meshes drawn by this node (one per
									 // primitive); see the owner for indexing
};

struct Scene
{
	std::vector<Mesh> meshes;
	// Node tree of the source file. ImportedNode::meshes index `meshes`.
	// Mesh::transform still holds each mesh's baked world transform.
	std::vector<ImportedNode> nodes;
	std::vector<Material> materials;
	std::vector<Texture> textures;
	LightEnvironment lights;
};

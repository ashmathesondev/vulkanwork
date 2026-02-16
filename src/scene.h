#pragma once

#include <vector>

#include "material.h"
#include "mesh.h"
#include "texture.h"

struct Scene
{
	std::vector<Mesh> meshes;
	std::vector<Material> materials;
	std::vector<Texture> textures;
};

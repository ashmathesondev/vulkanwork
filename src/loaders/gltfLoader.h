#pragma once

#include <string>

#include "graphics/scene.h"

Scene load_gltf(const std::string& path);

void compute_tangents(std::vector<Vertex>& verts,
					  const std::vector<uint32_t>& indices);

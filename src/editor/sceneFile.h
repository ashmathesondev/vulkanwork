#pragma once

#include <map>
#include <string>

#include "graphics/camera.h"
#include "graphics/light.h"
#include "graphics/material.h"
#include "sceneGraph.h"

struct SceneFileData
{
	std::string modelPath;
	SceneGraph sceneGraph;
	Camera camera;
	LightEnvironment lights;
	// Editor-changed materials, keyed by index into sceneGraph.nodes
	std::map<uint32_t, MaterialParams> nodeMaterials;
};

bool save_scene_file(const std::string& path, const SceneFileData& data);
bool load_scene_file(const std::string& path, SceneFileData& data);

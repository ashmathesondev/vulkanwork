#pragma once

#include <string>

#include "graphics/camera.h"
#include "graphics/light.h"
#include "sceneGraph.h"

struct SceneFileData
{
	std::string modelPath;
	SceneGraph sceneGraph;
	Camera camera;
	LightEnvironment lights;
};

bool save_scene_file(const std::string& path, const SceneFileData& data);
bool load_scene_file(const std::string& path, SceneFileData& data);

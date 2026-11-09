#pragma once

#include <string>

#include "graphics/scene.h"

// Loads FBX and COLLADA models. Desktop only: AssImp is not built for Android.
Scene load_assimp(const std::string& path);

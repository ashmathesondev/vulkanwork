#pragma once

#include <glm/glm.hpp>

struct Gizmo
{
	enum class Op
	{
		Translate,
		Rotate,
		Scale
	};
	enum class Space
	{
		World,
		Local
	};

	Op operation = Op::Translate;
	Space space = Space::World;
	bool useSnap = false;
	float snapTranslate = 0.5f;
	float snapRotate = 15.0f;
	float snapScale = 0.1f;

	void begin_frame();

	bool manipulate(const glm::mat4& view, const glm::mat4& proj,
					glm::mat4& objectMatrix, float viewportX, float viewportY,
					float viewportW, float viewportH);

	bool is_using() const;
};

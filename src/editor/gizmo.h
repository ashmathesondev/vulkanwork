#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "graphics/debugLines.h"

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
	float snapTranslate = .5f, snapRotate = 15.f, snapScale = .1f;
	void begin_frame();
	void input(glm::vec2 mouse, bool down, bool enabled);
	void cancel();
	bool manipulate(const glm::mat4&, const glm::mat4&, glm::mat4&, float,
					float, float, float);
	bool is_using() const { return active_ >= 0; }
	bool is_over() const { return hover_ >= 0; }
	const std::vector<LineVertex>& lines() const { return lines_; }

   private:
	glm::vec2 mouse_{}, press_{}, screenAxis_{};
	glm::vec3 axis_{};
	glm::mat4 start_{1};
	bool down_ = false, wasDown_ = false, enabled_ = false;
	int active_ = -1, hover_ = -1;
	float length_ = 1;
	Op dragOp_ = Op::Translate;
	std::vector<LineVertex> lines_;
};

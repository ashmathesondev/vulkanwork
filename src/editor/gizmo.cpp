#include "gizmo.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

void Gizmo::begin_frame()
{
	lines_.clear();
	hover_ = -1;
}
void Gizmo::cancel()
{
	active_ = hover_ = -1;
	wasDown_ = down_;
}
void Gizmo::input(glm::vec2 mouse, bool down, bool enabled)
{
	mouse_ = mouse;
	wasDown_ = down_;
	down_ = down;
	enabled_ = enabled;
	if (!down || !enabled) active_ = -1;
}

bool Gizmo::manipulate(const glm::mat4& view, const glm::mat4& proj,
					   glm::mat4& matrix, float vx, float vy, float vw,
					   float vh)
{
	glm::vec3 center(matrix[3]);
	const auto project = [&](glm::vec3 p)
	{
		auto q = proj * view * glm::vec4(p, 1);
		if (q.w <= .001f) return glm::vec2(-100000);
		return glm::vec2(vx + (q.x / q.w * .5f + .5f) * vw,
						 vy + (.5f - q.y / q.w * .5f) * vh);
	};
	float depth = -(view * glm::vec4(center, 1)).z;
	if (depth <= .001f)
	{
		cancel();
		return false;
	}
	float len = std::max(.01f, depth * 180.f / (std::abs(proj[1][1]) * vh));
	glm::vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	if (space == Space::Local)
		for (int i = 0; i < 3; ++i)
		{
			glm::vec3 a(matrix[i]);
			if (glm::length(a) > .00001f) axes[i] = glm::normalize(a);
		}
	float nearest = 9.f;
	glm::vec2 tangent(90, 0);
	for (int i = 0; i < 3; ++i)
	{
		glm::vec3 color = i == 0   ? glm::vec3(1, .2f, .2f)
						  : i == 1 ? glm::vec3(.2f, 1, .2f)
								   : glm::vec3(.3f, .5f, 1);
		if (active_ == i) color = {1, 1, 0};
		auto segment = [&](glm::vec3 a, glm::vec3 b)
		{
			lines_.push_back({a, color});
			lines_.push_back({b, color});
			auto p = project(a), q = project(b), d = q - p;
			float dd = glm::dot(d, d);
			if (dd < .01f || !enabled_) return;
			float t = std::clamp(glm::dot(mouse_ - p, d) / dd, 0.f, 1.f);
			float distance = glm::length(mouse_ - (p + t * d));
			if (distance < nearest)
			{
				nearest = distance;
				hover_ = i;
				tangent = glm::normalize(d) * 90.f;
			}
		};
		if (operation == Op::Rotate)
		{
			auto u = axes[(i + 1) % 3],
				 v = glm::normalize(glm::cross(axes[i], u));
			for (int j = 0; j < 64; ++j)
			{
				float a = j * glm::two_pi<float>() / 64,
					  b = (j + 1) * glm::two_pi<float>() / 64;
				segment(center + len * (u * std::cos(a) + v * std::sin(a)),
						center + len * (u * std::cos(b) + v * std::sin(b)));
			}
		}
		else
		{
			segment(center + axes[i] * len * .15f, center + axes[i] * len);
			auto side = axes[(i + 1) % 3] * len * .08f;
			segment(center + axes[i] * len,
					center + axes[i] * len * .8f + side);
			segment(center + axes[i] * len,
					center + axes[i] * len * .8f - side);
		}
	}
	if (enabled_ && down_ && !wasDown_ && hover_ >= 0)
	{
		active_ = hover_;
		start_ = matrix;
		press_ = mouse_;
		axis_ = axes[active_];
		screenAxis_ = project(center + axis_ * len) - project(center);
		if (operation == Op::Rotate) screenAxis_ = tangent;
		if (glm::length(screenAxis_) < 8.f) screenAxis_ = {90, 0};
		length_ = len;
		dragOp_ = operation;
	}
	if (active_ < 0 || !enabled_ || !down_) return false;
	float amount = glm::dot(mouse_ - press_, screenAxis_) /
				   glm::dot(screenAxis_, screenAxis_);
	float step = dragOp_ == Op::Translate ? snapTranslate
				 : dragOp_ == Op::Rotate  ? snapRotate
										  : snapScale;
	float value = amount * (dragOp_ == Op::Translate ? length_
							: dragOp_ == Op::Rotate	 ? 180.f
													 : 1.f);
	if (useSnap && step > 0) value = std::round(value / step) * step;
	glm::vec3 origin(start_[3]);
	if (dragOp_ == Op::Translate)
		matrix = glm::translate(glm::mat4(1), axis_ * value) * start_;
	else
	{
		glm::mat4 edit(1);
		if (dragOp_ == Op::Rotate)
			edit = glm::rotate(edit, glm::radians(value), axis_);
		else
		{
			float factor = std::max(.001f, 1 + value);
			glm::vec4 a(axis_, 0);
			edit += (factor - 1) * glm::outerProduct(a, a);
		}
		matrix = glm::translate(glm::mat4(1), origin) * edit *
				 glm::translate(glm::mat4(1), -origin) * start_;
	}
	return true;
}

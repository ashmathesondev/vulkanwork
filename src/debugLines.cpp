#include "debugLines.h"

#include <cmath>

static constexpr float PI = 3.14159265358979323846f;

// Add a single line segment
static void add_line(std::vector<LineVertex>& out, const glm::vec3& a,
					 const glm::vec3& b, const glm::vec3& color)
{
	out.push_back({a, color});
	out.push_back({b, color});
}

// Generate a circle of line segments in a plane defined by two axes
static void add_circle(std::vector<LineVertex>& out, const glm::vec3& center,
					   const glm::vec3& axisU, const glm::vec3& axisV,
					   float radius, const glm::vec3& color, int segments = 32)
{
	for (int i = 0; i < segments; ++i)
	{
		float a0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segments);
		float a1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segments);
		glm::vec3 p0 = center + radius * (std::cos(a0) * axisU + std::sin(a0) * axisV);
		glm::vec3 p1 = center + radius * (std::cos(a1) * axisU + std::sin(a1) * axisV);
		add_line(out, p0, p1, color);
	}
}

// Build an orthonormal basis from a direction vector
static void build_basis(const glm::vec3& dir, glm::vec3& outU, glm::vec3& outV)
{
	glm::vec3 d = glm::normalize(dir);
	// Pick a non-parallel vector for cross product
	glm::vec3 up = (std::abs(d.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
	outU = glm::normalize(glm::cross(d, up));
	outV = glm::normalize(glm::cross(d, outU));
}

static void generate_directional(std::vector<LineVertex>& out,
								 const DirectionalLight& light)
{
	glm::vec3 color = light.color;
	glm::vec3 dir = glm::normalize(light.direction);
	glm::vec3 origin(0.0f, 5.0f, 0.0f);  // Place above world origin for visibility
	float shaftLen = 2.0f;
	float headLen = 0.4f;
	float headRadius = 0.15f;

	glm::vec3 tip = origin + dir * shaftLen;

	// Shaft
	add_line(out, origin, tip, color);

	// Arrowhead ribs
	glm::vec3 u, v;
	build_basis(dir, u, v);
	glm::vec3 headBase = tip - dir * headLen;
	for (int i = 0; i < 4; ++i)
	{
		float angle = 2.0f * PI * static_cast<float>(i) / 4.0f;
		glm::vec3 ribEnd = headBase + headRadius * (std::cos(angle) * u + std::sin(angle) * v);
		add_line(out, tip, ribEnd, color);
	}

	// Arrowhead base circle
	add_circle(out, headBase, u, v, headRadius, color, 8);
}

static void generate_point(std::vector<LineVertex>& out,
						   const PointLight& light)
{
	glm::vec3 color = light.color;
	glm::vec3 pos = light.position;
	float r = light.radius;

	// Three axis-aligned circles
	add_circle(out, pos, {1, 0, 0}, {0, 1, 0}, r, color, 32);  // XY
	add_circle(out, pos, {1, 0, 0}, {0, 0, 1}, r, color, 32);  // XZ
	add_circle(out, pos, {0, 1, 0}, {0, 0, 1}, r, color, 32);  // YZ

	// Small cross at center for visibility
	float s = 0.15f;
	add_line(out, pos - glm::vec3(s, 0, 0), pos + glm::vec3(s, 0, 0), color);
	add_line(out, pos - glm::vec3(0, s, 0), pos + glm::vec3(0, s, 0), color);
	add_line(out, pos - glm::vec3(0, 0, s), pos + glm::vec3(0, 0, s), color);
}

static void generate_spot(std::vector<LineVertex>& out,
						  const SpotLight& light)
{
	glm::vec3 color = light.color;
	glm::vec3 pos = light.position;
	glm::vec3 dir = glm::normalize(light.direction);
	float r = light.radius;

	glm::vec3 u, v;
	build_basis(dir, u, v);

	// Outer cone
	float outerR = r * std::tan(light.outerConeAngle);
	glm::vec3 baseCenter = pos + dir * r;
	add_circle(out, baseCenter, u, v, outerR, color, 32);

	// Inner cone circle
	float innerR = r * std::tan(light.innerConeAngle);
	add_circle(out, baseCenter, u, v, innerR, color * 0.6f, 16);

	// Ribs from apex to outer cone base
	for (int i = 0; i < 8; ++i)
	{
		float angle = 2.0f * PI * static_cast<float>(i) / 8.0f;
		glm::vec3 rimPoint = baseCenter + outerR * (std::cos(angle) * u + std::sin(angle) * v);
		add_line(out, pos, rimPoint, color);
	}

	// Direction line through center
	add_line(out, pos, baseCenter, color * 0.5f);
}

std::vector<LineVertex> generate_light_lines(const LightEnvironment& lights)
{
	std::vector<LineVertex> verts;
	// Reserve a reasonable amount
	verts.reserve(512);

	for (const auto& d : lights.directionals)
		generate_directional(verts, d);

	for (const auto& p : lights.points)
		generate_point(verts, p);

	for (const auto& s : lights.spots)
		generate_spot(verts, s);

	return verts;
}

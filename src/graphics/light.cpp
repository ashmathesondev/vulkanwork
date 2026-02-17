#include "light.h"

uint32_t LightEnvironment::total_light_count() const
{
	return static_cast<uint32_t>(directionals.size() + points.size() +
								 spots.size());
}

std::vector<GPULight> LightEnvironment::pack_gpu_lights() const
{
	std::vector<GPULight> out;
	out.reserve(total_light_count());

	for (const auto& d : directionals)
	{
		GPULight g{};
		g.positionAndType = glm::vec4(
			0.0f, 0.0f, 0.0f, static_cast<float>(LightType::Directional));
		g.directionAndRadius = glm::vec4(glm::normalize(d.direction), FLT_MAX);
		g.colorAndIntensity = glm::vec4(d.color, d.intensity);
		g.coneParams = glm::vec4(0.0f);
		out.push_back(g);
	}

	for (const auto& p : points)
	{
		GPULight g{};
		g.positionAndType =
			glm::vec4(p.position, static_cast<float>(LightType::Point));
		g.directionAndRadius = glm::vec4(0.0f, 0.0f, 0.0f, p.radius);
		g.colorAndIntensity = glm::vec4(p.color, p.intensity);
		g.coneParams = glm::vec4(0.0f);
		out.push_back(g);
	}

	for (const auto& s : spots)
	{
		GPULight g{};
		g.positionAndType =
			glm::vec4(s.position, static_cast<float>(LightType::Spot));
		g.directionAndRadius = glm::vec4(glm::normalize(s.direction), s.radius);
		g.colorAndIntensity = glm::vec4(s.color, s.intensity);
		g.coneParams = glm::vec4(std::cos(s.innerConeAngle),
								 std::cos(s.outerConeAngle), 0.0f, 0.0f);
		out.push_back(g);
	}

	return out;
}

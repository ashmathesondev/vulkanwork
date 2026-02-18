#include "sceneFile.h"

#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include "logger.h"

using json = nlohmann::json;

// =============================================================================
// Helpers
// =============================================================================

static json vec3_to_json(const glm::vec3& v)
{
	return json::array({v.x, v.y, v.z});
}

static glm::vec3 json_to_vec3(const json& j)
{
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

static json mat4_to_json(const glm::mat4& m)
{
	json arr = json::array();
	const float* p = glm::value_ptr(m);
	for (int i = 0; i < 16; ++i) arr.push_back(p[i]);
	return arr;
}

static glm::mat4 json_to_mat4(const json& j)
{
	glm::mat4 m;
	float* p = glm::value_ptr(m);
	for (int i = 0; i < 16; ++i) p[i] = j[i].get<float>();
	return m;
}

// =============================================================================
// Save
// =============================================================================

bool save_scene_file(const std::string& path, const SceneFileData& data)
{
	LOG_INFO("save_scene_file: writing %zu nodes to '%s'",
			 data.sceneGraph.nodes.size(), path.c_str());
	json root;
	root["version"] = 1;
	root["modelPath"] = data.modelPath;

	// Camera
	root["camera"] = {
		{"position", vec3_to_json(data.camera.position)},
		{"yaw", data.camera.yaw},
		{"pitch", data.camera.pitch},
		{"fov", data.camera.fov},
	};

	// Lights
	json lightsObj;
	lightsObj["ambient"] = {
		{"color", vec3_to_json(data.lights.ambient.color)},
		{"intensity", data.lights.ambient.intensity},
	};

	json dirs = json::array();
	for (const auto& d : data.lights.directionals)
	{
		dirs.push_back({
			{"direction", vec3_to_json(d.direction)},
			{"color", vec3_to_json(d.color)},
			{"intensity", d.intensity},
		});
	}
	lightsObj["directionals"] = dirs;

	json pts = json::array();
	for (const auto& p : data.lights.points)
	{
		pts.push_back({
			{"position", vec3_to_json(p.position)},
			{"color", vec3_to_json(p.color)},
			{"intensity", p.intensity},
			{"radius", p.radius},
		});
	}
	lightsObj["points"] = pts;

	json spts = json::array();
	for (const auto& s : data.lights.spots)
	{
		spts.push_back({
			{"position", vec3_to_json(s.position)},
			{"direction", vec3_to_json(s.direction)},
			{"color", vec3_to_json(s.color)},
			{"intensity", s.intensity},
			{"radius", s.radius},
			{"innerConeAngle", s.innerConeAngle},
			{"outerConeAngle", s.outerConeAngle},
		});
	}
	lightsObj["spots"] = spts;
	root["lights"] = lightsObj;

	// Scene graph nodes
	json nodesArr = json::array();
	for (const auto& node : data.sceneGraph.nodes)
	{
		json n;
		n["name"] = node.name;
		n["localTransform"] = mat4_to_json(node.localTransform);
		n["meshIndex"] = node.meshIndex.has_value()
							 ? json(node.meshIndex.value())
							 : json(nullptr);
		n["parent"] =
			node.parent.has_value() ? json(node.parent.value()) : json(nullptr);

		json children = json::array();
		for (uint32_t c : node.children) children.push_back(c);
		n["children"] = children;

		nodesArr.push_back(n);
	}
	root["nodes"] = nodesArr;

	std::ofstream f(path);
	if (!f)
	{
		LOG_ERROR("save_scene_file: cannot open '%s' for writing",
				  path.c_str());
		return false;
	}
	f << root.dump(2);
	return f.good();
}

// =============================================================================
// Load
// =============================================================================

bool load_scene_file(const std::string& path, SceneFileData& data)
{
	std::ifstream f(path);
	if (!f)
	{
		LOG_ERROR("load_scene_file: cannot open '%s'", path.c_str());
		return false;
	}

	json root;
	try
	{
		root = json::parse(f);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("load_scene_file: JSON parse error in '%s': %s", path.c_str(),
				  e.what());
		return false;
	}

	data.modelPath = root.value("modelPath", std::string{});

	// Camera
	if (root.contains("camera"))
	{
		const auto& cam = root["camera"];
		if (cam.contains("position"))
			data.camera.position = json_to_vec3(cam["position"]);
		data.camera.yaw = cam.value("yaw", -90.0f);
		data.camera.pitch = cam.value("pitch", 0.0f);
		data.camera.fov = cam.value("fov", 45.0f);

		// Reconstruct front vector from yaw/pitch
		glm::vec3 d;
		d.x = std::cos(glm::radians(data.camera.yaw)) *
			  std::cos(glm::radians(data.camera.pitch));
		d.y = std::sin(glm::radians(data.camera.pitch));
		d.z = std::sin(glm::radians(data.camera.yaw)) *
			  std::cos(glm::radians(data.camera.pitch));
		data.camera.front = glm::normalize(d);
	}

	// Lights
	if (root.contains("lights"))
	{
		const auto& lts = root["lights"];
		if (lts.contains("ambient"))
		{
			const auto& amb = lts["ambient"];
			if (amb.contains("color"))
				data.lights.ambient.color = json_to_vec3(amb["color"]);
			data.lights.ambient.intensity = amb.value("intensity", 0.03f);
		}

		data.lights.directionals.clear();
		if (lts.contains("directionals"))
		{
			for (const auto& d : lts["directionals"])
			{
				DirectionalLight dl;
				if (d.contains("direction"))
					dl.direction = json_to_vec3(d["direction"]);
				if (d.contains("color")) dl.color = json_to_vec3(d["color"]);
				dl.intensity = d.value("intensity", 1.0f);
				data.lights.directionals.push_back(dl);
			}
		}

		data.lights.points.clear();
		if (lts.contains("points"))
		{
			for (const auto& p : lts["points"])
			{
				PointLight pl;
				if (p.contains("position"))
					pl.position = json_to_vec3(p["position"]);
				if (p.contains("color")) pl.color = json_to_vec3(p["color"]);
				pl.intensity = p.value("intensity", 1.0f);
				pl.radius = p.value("radius", 10.0f);
				data.lights.points.push_back(pl);
			}
		}

		data.lights.spots.clear();
		if (lts.contains("spots"))
		{
			for (const auto& s : lts["spots"])
			{
				SpotLight sl;
				if (s.contains("position"))
					sl.position = json_to_vec3(s["position"]);
				if (s.contains("direction"))
					sl.direction = json_to_vec3(s["direction"]);
				if (s.contains("color")) sl.color = json_to_vec3(s["color"]);
				sl.intensity = s.value("intensity", 1.0f);
				sl.radius = s.value("radius", 10.0f);
				sl.innerConeAngle =
					s.value("innerConeAngle", glm::radians(25.0f));
				sl.outerConeAngle =
					s.value("outerConeAngle", glm::radians(35.0f));
				data.lights.spots.push_back(sl);
			}
		}
	}

	// Scene graph nodes
	data.sceneGraph.nodes.clear();
	data.sceneGraph.roots.clear();
	if (root.contains("nodes"))
	{
		for (const auto& n : root["nodes"])
		{
			SceneNode node;
			node.name = n.value("name", std::string{});
			if (n.contains("localTransform"))
				node.localTransform = json_to_mat4(n["localTransform"]);
			node.worldTransform = node.localTransform;

			if (n.contains("meshIndex") && !n["meshIndex"].is_null())
				node.meshIndex = n["meshIndex"].get<uint32_t>();

			if (n.contains("parent") && !n["parent"].is_null())
				node.parent = n["parent"].get<uint32_t>();

			if (n.contains("children"))
			{
				for (const auto& c : n["children"])
					node.children.push_back(c.get<uint32_t>());
			}

			data.sceneGraph.nodes.push_back(std::move(node));
		}

		// Rebuild roots list
		for (uint32_t i = 0;
			 i < static_cast<uint32_t>(data.sceneGraph.nodes.size()); ++i)
		{
			if (!data.sceneGraph.nodes[i].parent.has_value())
				data.sceneGraph.roots.push_back(i);
		}
	}

	return true;
}

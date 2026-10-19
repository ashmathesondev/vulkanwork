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

static json light_to_json(const LightComponent& l)
{
	return {
		{"type", light_type_name(l.type)},
		{"color", vec3_to_json(l.color)},
		{"intensity", l.intensity},
		{"radius", l.radius},
		{"innerConeAngle", l.innerConeAngle},
		{"outerConeAngle", l.outerConeAngle},
	};
}

static bool json_to_light(const json& j, LightComponent& out)
{
	std::string type = j.value("type", std::string{});
	if (type == "Directional")
		out.type = LightType::Directional;
	else if (type == "Point")
		out.type = LightType::Point;
	else if (type == "Spot")
		out.type = LightType::Spot;
	else
		return false;

	if (j.contains("color")) out.color = json_to_vec3(j["color"]);
	out.intensity = j.value("intensity", 1.0f);
	out.radius = j.value("radius", 10.0f);
	out.innerConeAngle = j.value("innerConeAngle", glm::radians(25.0f));
	out.outerConeAngle = j.value("outerConeAngle", glm::radians(35.0f));
	return true;
}

// =============================================================================
// Save
// =============================================================================

bool save_scene_file(const std::string& path, const SceneFileData& data)
{
	LOG_INFO("save_scene_file: writing %zu nodes to '%s'",
			 data.sceneGraph.nodes.size(), path.c_str());
	json root;
	root["version"] = 2;
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

	// Directional/point/spot lights are saved on their scene nodes.
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
		n["modelPath"] = node.modelPath;
		n["meshIndexInModel"] = node.meshIndexInModel;
		n["parent"] =
			node.parent.has_value() ? json(node.parent.value()) : json(nullptr);
		if (node.light.has_value()) n["light"] = light_to_json(*node.light);

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

		// Version 1 kept typed light arrays here. They are converted to light
		// nodes once the node list has been read.
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

			node.modelPath = n.value("modelPath", std::string{});
			node.meshIndexInModel = n.value("meshIndexInModel", 0u);

			if (n.contains("parent") && !n["parent"].is_null())
				node.parent = n["parent"].get<uint32_t>();

			if (n.contains("light"))
			{
				LightComponent light;
				if (json_to_light(n["light"], light))
					node.light = light;
				else
					LOG_WARN(
						"load_scene_file: node '%s' has unknown light type",
						node.name.c_str());
			}

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

	// Version 1 files: turn legacy light arrays into root light nodes.
	uint32_t legacyCount = 0;
	for (size_t i = 0; i < data.lights.directionals.size(); ++i)
	{
		const auto& d = data.lights.directionals[i];
		LightComponent l;
		l.type = LightType::Directional;
		l.color = d.color;
		l.intensity = d.intensity;
		data.sceneGraph.add_light_node(
			"Directional Light " + std::to_string(i), l,
			make_light_transform(d.position, d.direction));
		++legacyCount;
	}
	for (size_t i = 0; i < data.lights.points.size(); ++i)
	{
		const auto& p = data.lights.points[i];
		LightComponent l;
		l.type = LightType::Point;
		l.color = p.color;
		l.intensity = p.intensity;
		l.radius = p.radius;
		data.sceneGraph.add_light_node(
			"Point Light " + std::to_string(i), l,
			make_light_transform(p.position, glm::vec3(0.0f, -1.0f, 0.0f)));
		++legacyCount;
	}
	for (size_t i = 0; i < data.lights.spots.size(); ++i)
	{
		const auto& sp = data.lights.spots[i];
		LightComponent l;
		l.type = LightType::Spot;
		l.color = sp.color;
		l.intensity = sp.intensity;
		l.radius = sp.radius;
		l.innerConeAngle = sp.innerConeAngle;
		l.outerConeAngle = sp.outerConeAngle;
		data.sceneGraph.add_light_node(
			"Spot Light " + std::to_string(i), l,
			make_light_transform(sp.position, sp.direction));
		++legacyCount;
	}
	data.lights.directionals.clear();
	data.lights.points.clear();
	data.lights.spots.clear();
	if (legacyCount > 0)
		LOG_INFO("load_scene_file: converted %u legacy light(s) to nodes",
				 legacyCount);

	return true;
}

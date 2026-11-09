#include "assimpLoader.h"

#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iterator>
#include <stdexcept>
#include <unordered_map>

#include "gltfLoader.h"
#include "logger.h"

namespace fs = std::filesystem;

namespace
{

struct ImportContext
{
	const aiScene& ai;
	fs::path modelDir;
	Scene& scene;
	std::unordered_map<std::string, int32_t> textureIndices;
};

// AssImp strings and the paths from the file dialog are UTF-8.
fs::path utf8_path(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

glm::mat4 to_glm(const aiMatrix4x4& m)
{
	// aiMatrix4x4 is row-major, glm is column-major
	return glm::transpose(glm::make_mat4(&m.a1));
}

bool decode_image(const uint8_t* bytes, int size, Texture& tex)
{
	int w = 0, h = 0, comp = 0;
	stbi_uc* data =
		stbi_load_from_memory(bytes, size, &w, &h, &comp, STBI_rgb_alpha);
	if (!data) return false;

	tex.width = static_cast<uint32_t>(w);
	tex.height = static_cast<uint32_t>(h);
	tex.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
	stbi_image_free(data);
	return true;
}

bool decode_embedded(const aiTexture& in, Texture& tex)
{
	// mHeight == 0 means pcData holds a compressed file of mWidth bytes
	if (in.mHeight == 0)
		return decode_image(reinterpret_cast<const uint8_t*>(in.pcData),
							static_cast<int>(in.mWidth), tex);

	tex.width = in.mWidth;
	tex.height = in.mHeight;
	tex.pixels.resize(static_cast<size_t>(in.mWidth) * in.mHeight * 4);
	for (size_t i = 0; i < static_cast<size_t>(in.mWidth) * in.mHeight; ++i)
	{
		const aiTexel& t = in.pcData[i];
		tex.pixels[i * 4] = t.r;
		tex.pixels[i * 4 + 1] = t.g;
		tex.pixels[i * 4 + 2] = t.b;
		tex.pixels[i * 4 + 3] = t.a;
	}
	return true;
}

bool load_image_file(const fs::path& path, Texture& tex)
{
	std::error_code ec;
	if (!fs::is_regular_file(path, ec)) return false;

	std::ifstream f(path, std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
							   std::istreambuf_iterator<char>());
	return !bytes.empty() &&
		   decode_image(bytes.data(), static_cast<int>(bytes.size()), tex);
}

// Rooted paths are never opened. They usually name the author's machine, and
// a UNC path would make Windows contact a server chosen by the model file.
bool load_external(const std::string& raw, const fs::path& modelDir,
				   Texture& tex)
{
	std::string normalized = raw;
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	fs::path p = utf8_path(normalized);

	if (!p.has_root_path() && load_image_file(modelDir / p, tex)) return true;
	return load_image_file(modelDir / p.filename(), tex);
}

// Returns the scene texture index, or -1 when absent or unreadable so that
// the renderer binds its default texture.
int32_t load_texture(ImportContext& ctx, const aiMaterial& mat,
					 aiTextureType type, bool srgb)
{
	aiString name;
	if (mat.GetTexture(type, 0, &name) != AI_SUCCESS) return -1;

	std::string key = (srgb ? "s:" : "l:") + std::string(name.C_Str());
	auto found = ctx.textureIndices.find(key);
	if (found != ctx.textureIndices.end()) return found->second;

	Texture tex;
	tex.isSrgb = srgb;
	const aiTexture* embedded = ctx.ai.GetEmbeddedTexture(name.C_Str());
	bool ok = embedded ? decode_embedded(*embedded, tex)
					   : load_external(name.C_Str(), ctx.modelDir, tex);

	int32_t index = -1;
	if (ok)
	{
		index = static_cast<int32_t>(ctx.scene.textures.size());
		ctx.scene.textures.push_back(std::move(tex));
	}
	else
	{
		LOG_WARN("Texture not found or unreadable: %s", name.C_Str());
	}
	ctx.textureIndices[key] = index;
	return index;
}

// FBX and COLLADA are mostly Phong or Lambert, so the metallic-roughness
// values are an approximation unless the file supplies PBR factors.
Material convert_material(ImportContext& ctx, const aiMaterial& in)
{
	Material out;
	out.metallicFactor = 0.0f;

	out.baseColorTexture = load_texture(ctx, in, aiTextureType_DIFFUSE, true);
	out.normalTexture = load_texture(ctx, in, aiTextureType_NORMALS, false);
	out.emissiveTexture = load_texture(ctx, in, aiTextureType_EMISSIVE, true);

	// Authoring tools ignore the color slot once a texture is connected, and
	// it is often left black, so it must not tint the texture.
	aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
	if (out.baseColorTexture < 0 &&
		in.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
		in.Get(AI_MATKEY_COLOR_DIFFUSE, color);
	float opacity = 1.0f;
	if (in.Get(AI_MATKEY_OPACITY, opacity) != AI_SUCCESS) opacity = color.a;
	out.baseColorFactor = glm::vec4(color.r, color.g, color.b, opacity);

	if (out.emissiveTexture >= 0)
	{
		out.emissiveFactor = glm::vec3(1.0f);
	}
	else
	{
		aiColor3D emissive(0.0f, 0.0f, 0.0f);
		in.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
		out.emissiveFactor = glm::vec3(emissive.r, emissive.g, emissive.b);
	}

	in.Get(AI_MATKEY_METALLIC_FACTOR, out.metallicFactor);
	float roughness = 0.0f;
	float shininess = 0.0f;
	if (in.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
		out.roughnessFactor = roughness;
	else if (in.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
		// Blinn-Phong exponent to GGX: alpha^2 = 2 / (s + 2), alpha = r^2
		out.roughnessFactor =
			std::pow(2.0f / (std::max(shininess, 0.0f) + 2.0f), 0.25f);
	else
		out.roughnessFactor = 0.8f;

	return out;
}

Mesh convert_mesh(const aiMesh& in, const glm::mat4& world)
{
	Mesh out;
	out.materialIndex = in.mMaterialIndex;
	out.transform = world;

	out.vertices.resize(in.mNumVertices);
	for (uint32_t i = 0; i < in.mNumVertices; ++i)
	{
		Vertex& v = out.vertices[i];
		v.pos =
			glm::vec3(in.mVertices[i].x, in.mVertices[i].y, in.mVertices[i].z);
		if (in.HasNormals())
			v.normal =
				glm::vec3(in.mNormals[i].x, in.mNormals[i].y, in.mNormals[i].z);
		if (in.HasTextureCoords(0))
			v.uv =
				glm::vec2(in.mTextureCoords[0][i].x, in.mTextureCoords[0][i].y);
		out.localBounds.expand(v.pos);
	}

	out.indices.reserve(static_cast<size_t>(in.mNumFaces) * 3);
	for (uint32_t i = 0; i < in.mNumFaces; ++i)
	{
		const aiFace& face = in.mFaces[i];
		if (face.mNumIndices != 3) continue;
		out.indices.insert(out.indices.end(), face.mIndices, face.mIndices + 3);
	}

	compute_tangents(out.vertices, out.indices);
	return out;
}

void extract_node(ImportContext& ctx, const aiNode& node,
				  const glm::mat4& parentTransform, int32_t parentImported,
				  const std::string& rootName)
{
	glm::mat4 local = to_glm(node.mTransformation);
	glm::mat4 world = parentTransform * local;
	Scene& scene = ctx.scene;

	uint32_t importedIdx = static_cast<uint32_t>(scene.nodes.size());
	{
		ImportedNode in;
		if (parentImported < 0)
			in.name = rootName;
		else if (node.mName.length > 0)
			in.name = node.mName.C_Str();
		else
			in.name = "Node " + std::to_string(importedIdx);
		in.localTransform = local;
		in.parent = parentImported;
		scene.nodes.push_back(std::move(in));
		if (parentImported >= 0)
			scene.nodes[static_cast<size_t>(parentImported)].children.push_back(
				importedIdx);
	}

	// Each node gets its own copy: Mesh::transform is per mesh, so a mesh
	// instanced by several nodes cannot be shared.
	for (uint32_t i = 0; i < node.mNumMeshes; ++i)
	{
		Mesh mesh = convert_mesh(*ctx.ai.mMeshes[node.mMeshes[i]], world);
		mesh.name = scene.nodes[importedIdx].name;
		if (node.mNumMeshes > 1)
			mesh.name += " [prim " + std::to_string(i) + "]";
		scene.nodes[importedIdx].meshes.push_back(
			static_cast<uint32_t>(scene.meshes.size()));
		scene.meshes.push_back(std::move(mesh));
	}

	for (uint32_t i = 0; i < node.mNumChildren; ++i)
		extract_node(ctx, *node.mChildren[i], world,
					 static_cast<int32_t>(importedIdx), rootName);
}

}  // namespace

Scene load_assimp(const std::string& path)
{
	Assimp::Importer importer;
	// Without this, FBX pivots add $AssimpFbx$ helper nodes to the hierarchy
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
								aiPrimitiveType_POINT | aiPrimitiveType_LINE);

	// Saved scenes address meshes by their index in this output, so changing
	// these flags can invalidate existing .scene files.
	const aiScene* ai = importer.ReadFile(
		path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
				  aiProcess_GenSmoothNormals | aiProcess_SortByPType |
				  aiProcess_FlipUVs | aiProcess_GlobalScale |
				  aiProcess_ValidateDataStructure);
	if (!ai || !ai->mRootNode || (ai->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
		throw std::runtime_error("Failed to load model: " + path + ": " +
								 importer.GetErrorString());

	Scene scene;
	ImportContext ctx{*ai, utf8_path(path).parent_path(), scene, {}};

	for (uint32_t i = 0; i < ai->mNumMaterials; ++i)
		scene.materials.push_back(convert_material(ctx, *ai->mMaterials[i]));

	std::u8string stem = utf8_path(path).stem().u8string();
	extract_node(ctx, *ai->mRootNode, glm::mat4{1.0f}, -1,
				 std::string(stem.begin(), stem.end()));

	LOG_INFO("Loaded '%s': %zu mesh(es), %zu material(s), %zu texture(s)",
			 path.c_str(), scene.meshes.size(), scene.materials.size(),
			 scene.textures.size());
	return scene;
}

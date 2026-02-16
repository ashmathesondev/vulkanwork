#include "gltfLoader.h"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <stb_image.h>
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

// =============================================================================
// Custom image loader for tinygltf (using stb_image)
// =============================================================================

static bool load_image_data(tinygltf::Image* image, const int image_idx,
							std::string* err, std::string* warn, int req_width,
							int req_height, const unsigned char* bytes,
							int size, void*)
{
	(void)image_idx;
	(void)warn;
	(void)req_width;
	(void)req_height;

	int w, h, comp;
	unsigned char* data =
		stbi_load_from_memory(bytes, size, &w, &h, &comp, STBI_rgb_alpha);
	if (!data)
	{
		if (err) *err = "stb_image: failed to decode image";
		return false;
	}

	image->width = w;
	image->height = h;
	image->component = 4;
	image->bits = 8;
	image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
	image->image.assign(data, data + static_cast<size_t>(w) * h * 4);

	stbi_image_free(data);
	return true;
}

// =============================================================================
// Helpers
// =============================================================================

static glm::mat4 node_transform(const tinygltf::Node& node)
{
	if (node.matrix.size() == 16)
	{
		return glm::make_mat4(node.matrix.data());
	}

	glm::mat4 m{1.0f};
	if (node.translation.size() == 3)
		m = glm::translate(m,
						   glm::vec3(static_cast<float>(node.translation[0]),
									 static_cast<float>(node.translation[1]),
									 static_cast<float>(node.translation[2])));
	if (node.rotation.size() == 4)
	{
		glm::quat q(static_cast<float>(node.rotation[3]),
					static_cast<float>(node.rotation[0]),
					static_cast<float>(node.rotation[1]),
					static_cast<float>(node.rotation[2]));
		m *= glm::mat4_cast(q);
	}
	if (node.scale.size() == 3)
		m = glm::scale(m, glm::vec3(static_cast<float>(node.scale[0]),
									static_cast<float>(node.scale[1]),
									static_cast<float>(node.scale[2])));
	return m;
}

template <typename T>
static const T* accessor_data(const tinygltf::Model& model,
							  const tinygltf::Accessor& acc)
{
	const auto& bv = model.bufferViews[acc.bufferView];
	const auto& buf = model.buffers[bv.buffer];
	return reinterpret_cast<const T*>(buf.data.data() + bv.byteOffset +
									  acc.byteOffset);
}

static int accessor_stride(const tinygltf::Model& model,
						   const tinygltf::Accessor& acc)
{
	const auto& bv = model.bufferViews[acc.bufferView];
	if (bv.byteStride > 0) return static_cast<int>(bv.byteStride);

	// Compute tight stride from type + componentType
	int compSize = 1;
	switch (acc.componentType)
	{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			compSize = 1;
			break;
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			compSize = 2;
			break;
		case TINYGLTF_COMPONENT_TYPE_INT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			compSize = 4;
			break;
		default:
			break;
	}
	int numComps = 1;
	switch (acc.type)
	{
		case TINYGLTF_TYPE_SCALAR:
			numComps = 1;
			break;
		case TINYGLTF_TYPE_VEC2:
			numComps = 2;
			break;
		case TINYGLTF_TYPE_VEC3:
			numComps = 3;
			break;
		case TINYGLTF_TYPE_VEC4:
			numComps = 4;
			break;
		case TINYGLTF_TYPE_MAT4:
			numComps = 16;
			break;
		default:
			break;
	}
	return compSize * numComps;
}

static void compute_tangents(std::vector<Vertex>& verts,
							 const std::vector<uint32_t>& indices)
{
	std::vector<glm::vec3> tan1(verts.size(), glm::vec3(0));
	std::vector<glm::vec3> tan2(verts.size(), glm::vec3(0));

	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
		const auto& v0 = verts[i0];
		const auto& v1 = verts[i1];
		const auto& v2 = verts[i2];

		glm::vec3 e1 = v1.pos - v0.pos;
		glm::vec3 e2 = v2.pos - v0.pos;
		glm::vec2 duv1 = v1.uv - v0.uv;
		glm::vec2 duv2 = v2.uv - v0.uv;

		float r = duv1.x * duv2.y - duv2.x * duv1.y;
		if (std::abs(r) < 1e-8f) continue;
		r = 1.0f / r;

		glm::vec3 sdir = (e1 * duv2.y - e2 * duv1.y) * r;
		glm::vec3 tdir = (e2 * duv1.x - e1 * duv2.x) * r;

		tan1[i0] += sdir;
		tan1[i1] += sdir;
		tan1[i2] += sdir;
		tan2[i0] += tdir;
		tan2[i1] += tdir;
		tan2[i2] += tdir;
	}

	for (size_t i = 0; i < verts.size(); ++i)
	{
		const glm::vec3& n = verts[i].normal;
		const glm::vec3& t = tan1[i];

		// Gram-Schmidt orthogonalize
		glm::vec3 ortho = glm::normalize(t - n * glm::dot(n, t));
		float w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
		verts[i].tangent = glm::vec4(ortho, w);
	}
}

// =============================================================================
// Extract mesh primitives from a glTF node
// =============================================================================

static void extract_node(const tinygltf::Model& model, int nodeIdx,
						 const glm::mat4& parentTransform, Scene& scene)
{
	const auto& node = model.nodes[nodeIdx];
	glm::mat4 transform = parentTransform * node_transform(node);

	if (node.mesh >= 0)
	{
		const auto& mesh = model.meshes[node.mesh];
		for (const auto& prim : mesh.primitives)
		{
			if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1)
				continue;

			Mesh cpuMesh;
			cpuMesh.transform = transform;
			cpuMesh.materialIndex =
				(prim.material >= 0) ? static_cast<uint32_t>(prim.material) : 0;

			// --- Indices ---
			if (prim.indices >= 0)
			{
				const auto& acc = model.accessors[prim.indices];
				const auto& bv = model.bufferViews[acc.bufferView];
				const auto& buf = model.buffers[bv.buffer];
				const uint8_t* base =
					buf.data.data() + bv.byteOffset + acc.byteOffset;

				cpuMesh.indices.resize(acc.count);
				for (size_t i = 0; i < acc.count; ++i)
				{
					switch (acc.componentType)
					{
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
							cpuMesh.indices[i] =
								reinterpret_cast<const uint16_t*>(base)[i];
							break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
							cpuMesh.indices[i] =
								reinterpret_cast<const uint32_t*>(base)[i];
							break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
							cpuMesh.indices[i] = base[i];
							break;
						default:
							throw std::runtime_error(
								"Unsupported index component type");
					}
				}
			}

			// --- Vertex attributes ---
			size_t vertexCount = 0;

			auto posIt = prim.attributes.find("POSITION");
			if (posIt != prim.attributes.end())
			{
				const auto& acc = model.accessors[posIt->second];
				vertexCount = acc.count;
				cpuMesh.vertices.resize(vertexCount);
				int stride = accessor_stride(model, acc);
				const uint8_t* base = reinterpret_cast<const uint8_t*>(
					accessor_data<uint8_t>(model, acc));
				for (size_t i = 0; i < vertexCount; ++i)
				{
					const float* p =
						reinterpret_cast<const float*>(base + i * stride);
					cpuMesh.vertices[i].pos = {p[0], p[1], p[2]};
				}
			}

			auto normIt = prim.attributes.find("NORMAL");
			if (normIt != prim.attributes.end())
			{
				const auto& acc = model.accessors[normIt->second];
				int stride = accessor_stride(model, acc);
				const uint8_t* base = reinterpret_cast<const uint8_t*>(
					accessor_data<uint8_t>(model, acc));
				for (size_t i = 0; i < vertexCount; ++i)
				{
					const float* p =
						reinterpret_cast<const float*>(base + i * stride);
					cpuMesh.vertices[i].normal = {p[0], p[1], p[2]};
				}
			}

			auto uvIt = prim.attributes.find("TEXCOORD_0");
			if (uvIt != prim.attributes.end())
			{
				const auto& acc = model.accessors[uvIt->second];
				int stride = accessor_stride(model, acc);
				const uint8_t* base = reinterpret_cast<const uint8_t*>(
					accessor_data<uint8_t>(model, acc));
				for (size_t i = 0; i < vertexCount; ++i)
				{
					const float* p =
						reinterpret_cast<const float*>(base + i * stride);
					cpuMesh.vertices[i].uv = {p[0], p[1]};
				}
			}

			auto tanIt = prim.attributes.find("TANGENT");
			if (tanIt != prim.attributes.end())
			{
				const auto& acc = model.accessors[tanIt->second];
				int stride = accessor_stride(model, acc);
				const uint8_t* base = reinterpret_cast<const uint8_t*>(
					accessor_data<uint8_t>(model, acc));
				for (size_t i = 0; i < vertexCount; ++i)
				{
					const float* p =
						reinterpret_cast<const float*>(base + i * stride);
					cpuMesh.vertices[i].tangent = {p[0], p[1], p[2], p[3]};
				}
			}
			else
			{
				compute_tangents(cpuMesh.vertices, cpuMesh.indices);
			}

			// Generate indices if none provided
			if (cpuMesh.indices.empty())
			{
				cpuMesh.indices.resize(vertexCount);
				for (size_t i = 0; i < vertexCount; ++i)
					cpuMesh.indices[i] = static_cast<uint32_t>(i);
			}

			scene.meshes.push_back(std::move(cpuMesh));
		}
	}

	for (int child : node.children)
		extract_node(model, child, transform, scene);
}

// =============================================================================
// Extract materials
// =============================================================================

static void extract_materials(const tinygltf::Model& model, Scene& scene)
{
	for (const auto& mat : model.materials)
	{
		Material pbr;

		const auto& pbr_mr = mat.pbrMetallicRoughness;
		pbr.baseColorFactor =
			glm::vec4(static_cast<float>(pbr_mr.baseColorFactor[0]),
					  static_cast<float>(pbr_mr.baseColorFactor[1]),
					  static_cast<float>(pbr_mr.baseColorFactor[2]),
					  static_cast<float>(pbr_mr.baseColorFactor[3]));
		pbr.metallicFactor = static_cast<float>(pbr_mr.metallicFactor);
		pbr.roughnessFactor = static_cast<float>(pbr_mr.roughnessFactor);

		if (pbr_mr.baseColorTexture.index >= 0)
			pbr.baseColorTexture =
				model.textures[pbr_mr.baseColorTexture.index].source;

		if (pbr_mr.metallicRoughnessTexture.index >= 0)
			pbr.metallicRoughnessTexture =
				model.textures[pbr_mr.metallicRoughnessTexture.index].source;

		if (mat.normalTexture.index >= 0)
			pbr.normalTexture = model.textures[mat.normalTexture.index].source;

		if (mat.emissiveTexture.index >= 0)
			pbr.emissiveTexture =
				model.textures[mat.emissiveTexture.index].source;

		pbr.emissiveFactor =
			glm::vec3(static_cast<float>(mat.emissiveFactor[0]),
					  static_cast<float>(mat.emissiveFactor[1]),
					  static_cast<float>(mat.emissiveFactor[2]));

		scene.materials.push_back(pbr);
	}

	// Ensure at least one default material
	if (scene.materials.empty())
	{
		scene.materials.push_back(Material{});
	}
}

// =============================================================================
// Extract images → CpuTexture
// =============================================================================

static void extract_textures(const tinygltf::Model& model, Scene& scene)
{
	for (const auto& img : model.images)
	{
		Texture tex;

		if (!img.image.empty() && img.width > 0 && img.height > 0)
		{
			// tinygltf already decoded the image
			tex.width = static_cast<uint32_t>(img.width);
			tex.height = static_cast<uint32_t>(img.height);

			if (img.component == 4)
			{
				tex.pixels.assign(img.image.begin(), img.image.end());
			}
			else
			{
				// Convert to RGBA
				tex.pixels.resize(tex.width * tex.height * 4);
				for (uint32_t i = 0; i < tex.width * tex.height; ++i)
				{
					for (int c = 0; c < img.component && c < 3; ++c)
						tex.pixels[i * 4 + c] =
							img.image[i * img.component + c];
					if (img.component >= 4)
						tex.pixels[i * 4 + 3] =
							img.image[i * img.component + 3];
					else
						tex.pixels[i * 4 + 3] = 255;
				}
			}
		}
		else
		{
			// 1x1 white fallback
			tex.width = 1;
			tex.height = 1;
			tex.pixels = {255, 255, 255, 255};
		}

		scene.textures.push_back(std::move(tex));
	}

	// Mark normal and metallic-roughness textures as non-sRGB
	for (const auto& mat : scene.materials)
	{
		if (mat.normalTexture >= 0 &&
			mat.normalTexture < static_cast<int32_t>(scene.textures.size()))
			scene.textures[mat.normalTexture].isSrgb = false;
		if (mat.metallicRoughnessTexture >= 0 &&
			mat.metallicRoughnessTexture <
				static_cast<int32_t>(scene.textures.size()))
			scene.textures[mat.metallicRoughnessTexture].isSrgb = false;
	}
}

// =============================================================================
// Public API
// =============================================================================

Scene load_gltf(const std::string& path)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	loader.SetImageLoader(load_image_data, nullptr);
	std::string err, warn;

	bool ok = false;
	if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
		ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
	else
		ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

	if (!warn.empty()) std::fprintf(stderr, "glTF warning: %s\n", warn.c_str());
	if (!err.empty()) std::fprintf(stderr, "glTF error: %s\n", err.c_str());
	if (!ok) throw std::runtime_error("Failed to load glTF: " + path);

	Scene scene;

	extract_textures(model, scene);
	extract_materials(model, scene);

	// Walk scene nodes
	const auto& gltfScene =
		model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
	for (int nodeIdx : gltfScene.nodes)
		extract_node(model, nodeIdx, glm::mat4{1.0f}, scene);

	std::fprintf(
		stderr, "Loaded glTF: %zu meshes, %zu materials, %zu textures\n",
		scene.meshes.size(), scene.materials.size(), scene.textures.size());

	return scene;
}

#include "gaussianSplatLoader.h"

#include <cstdio>
#include <cstring>

#include "plyLoader.h"

// =============================================================================
// 3DGS PLY property name helpers
// =============================================================================

// Reads a float property from a row; returns 0.f if not present.
static float read_float(const void* rowBytes, size_t offset, size_t propSize)
{
	if (offset == SIZE_MAX || propSize != 4) return 0.f;
	float v;
	std::memcpy(&v, static_cast<const char*>(rowBytes) + offset, 4);
	return v;
}

// =============================================================================
// load_gaussian_splat_ply
// =============================================================================

GaussianSplatCloud load_gaussian_splat_ply(const std::string& path)
{
	GaussianSplatCloud cloud;

	PlyReader r;
	if (!r.open(path))
	{
		std::fprintf(stderr, "[GaussianSplatLoader] Failed to open: %s\n",
					 path.c_str());
		return cloud;
	}

	uint32_t N = r.element_count();
	if (N == 0) return cloud;

	// ---- Locate properties --------------------------------------------------

	// Position
	size_t off_x = r.property_offset("x");
	size_t off_y = r.property_offset("y");
	size_t off_z = r.property_offset("z");

	// Opacity
	size_t off_opacity = r.property_offset("opacity");

	// Rotation (wxyz)
	size_t off_rot[4];
	off_rot[0] = r.property_offset("rot_0");
	off_rot[1] = r.property_offset("rot_1");
	off_rot[2] = r.property_offset("rot_2");
	off_rot[3] = r.property_offset("rot_3");

	// Scale
	size_t off_scale[3];
	off_scale[0] = r.property_offset("scale_0");
	off_scale[1] = r.property_offset("scale_1");
	off_scale[2] = r.property_offset("scale_2");

	// SH: DC (f_dc_0, f_dc_1, f_dc_2) + rest (f_rest_0 .. f_rest_44)
	size_t off_dc[3];
	off_dc[0] = r.property_offset("f_dc_0");
	off_dc[1] = r.property_offset("f_dc_1");
	off_dc[2] = r.property_offset("f_dc_2");

	size_t off_rest[45];
	char buf[32];
	for (int i = 0; i < 45; ++i)
	{
		std::snprintf(buf, sizeof(buf), "f_rest_%d", i);
		off_rest[i] = r.property_offset(buf);
	}

	bool hasPosition =
		(off_x != SIZE_MAX && off_y != SIZE_MAX && off_z != SIZE_MAX);
	if (!hasPosition)
	{
		std::fprintf(stderr,
					 "[GaussianSplatLoader] Missing position fields in %s\n",
					 path.c_str());
		return cloud;
	}

	// ---- Fill splats --------------------------------------------------------
	cloud.splats.resize(N);
	cloud.sourcePath = path;

	for (uint32_t i = 0; i < N; ++i)
	{
		const void* row = r.row_data(i);
		RawSplat& s = cloud.splats[i];

		s.position.x = read_float(row, off_x, 4);
		s.position.y = read_float(row, off_y, 4);
		s.position.z = read_float(row, off_z, 4);

		s.opacity = read_float(row, off_opacity, 4);

		// rotation: wxyz
		s.rotation.x = read_float(row, off_rot[0], 4);
		s.rotation.y = read_float(row, off_rot[1], 4);
		s.rotation.z = read_float(row, off_rot[2], 4);
		s.rotation.w = read_float(row, off_rot[3], 4);

		s.scale.x = read_float(row, off_scale[0], 4);
		s.scale.y = read_float(row, off_scale[1], 4);
		s.scale.z = read_float(row, off_scale[2], 4);

		s._pad = 0.f;

		// sh layout in RawSplat:
		//   [0]  = R_dc  (f_dc_0)
		//   [1]  = G_dc  (f_dc_1)
		//   [2]  = B_dc  (f_dc_2)
		//   [3..17]  = R higher-order l=1..3  (f_rest_0..14)
		//   [18..32] = G higher-order l=1..3  (f_rest_15..29)
		//   [33..47] = B higher-order l=1..3  (f_rest_30..44)
		s.sh[0] = read_float(row, off_dc[0], 4);
		s.sh[1] = read_float(row, off_dc[1], 4);
		s.sh[2] = read_float(row, off_dc[2], 4);

		for (int k = 0; k < 45; ++k)
			s.sh[3 + k] = read_float(row, off_rest[k], 4);
	}

	std::fprintf(stdout, "[GaussianSplatLoader] Loaded %u splats from %s\n", N,
				 path.c_str());
	return cloud;
}

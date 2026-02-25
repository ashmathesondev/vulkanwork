# Gaussian Splatting — Lesson Plan

## 1. What is 3D Gaussian Splatting?

3D Gaussian Splatting (3DGS), introduced by Kerbl et al. in 2023 (*"3D Gaussian Splatting
for Real-Time Radiance Field Rendering"*), is a novel scene representation and rendering
technique for photo-realistic novel-view synthesis.

**Point clouds vs. NeRF vs. 3DGS**

| Approach | Representation | Rendering |
|----------|----------------|-----------|
| Point cloud | Discrete colored points | Fast but aliased, no soft edges |
| NeRF | Implicit MLP density field | Photorealistic but slow (ray marching) |
| 3DGS | Millions of 3D Gaussian blobs | Photorealistic **and** real-time |

Each Gaussian is a semi-transparent ellipsoidal blob in 3D space. The scene is initialized
from a sparse Structure-from-Motion (SfM) point cloud, then the Gaussians are optimized
with gradient descent (including adaptive densification and pruning) to reproduce a set of
training photographs. The result is a compact, unstructured set of a few million Gaussians
that can be rasterized in real time on commodity GPUs.

Unlike NeRF, 3DGS produces an explicit point-cloud-like structure that can be rendered
purely with rasterization — no ray marching, no MLP evaluation at runtime.

---

## 2. Splat Data: What Does Each Gaussian Store?

Every Gaussian has these attributes:

| Attribute | Size | Storage | Description |
|-----------|------|---------|-------------|
| **Position** | 3 floats | as-is | World-space center (x, y, z) |
| **Rotation** | 4 floats | unit quaternion (w, x, y, z) | Orientation of the ellipsoid |
| **Scale** | 3 floats | log-space: `s = exp(stored)` | Half-axes of the ellipsoid |
| **Opacity** | 1 float | logit-space: `α = sigmoid(stored)` | Transmittance |
| **SH coefficients** | 48 floats | linear | Color as spherical harmonics (degree 0–3, 3 channels) |
| **Total** | **60 floats** | | **240 bytes** per splat |

**Why log-scale and logit-opacity?**
The optimizer can push scale and opacity to any real value without clamping during
gradient descent. `exp(·)` enforces scale > 0, `sigmoid(·)` enforces α ∈ (0, 1).
The de-quantized values are computed at render time (in `gsplat_preprocess.comp`).

**Why quaternion + scale instead of a full covariance matrix?**
The 3×3 covariance Σ = R · diag(s²) · Rᵀ is positive-semi-definite by construction.
Storing the factored form (R, s) prevents the matrix from becoming indefinite under
unconstrained gradient updates, which would make the Gaussian undefined.

---

## 3. PLY File Format and 3DGS Property Names

3DGS scenes are exported as **binary-little-endian PLY** (Polygon File Format) files
with a single `element vertex` block — one row per Gaussian.

### Header structure
```
ply
format binary_little_endian 1.0
element vertex 3211485
property float x
property float y
property float z
property float nx             ← legacy placeholder, always 0, ignored
property float ny
property float nz
property float f_dc_0         ← degree-0 (DC) SH coefficient for R
property float f_dc_1         ← degree-0 (DC) SH coefficient for G
property float f_dc_2         ← degree-0 (DC) SH coefficient for B
property float f_rest_0       ← higher-order SH for R, l=1,2,3 (15 values)
...
property float f_rest_14
property float f_rest_15      ← higher-order SH for G (15 values)
...
property float f_rest_29
property float f_rest_30      ← higher-order SH for B (15 values)
...
property float f_rest_44
property float opacity        ← logit-space opacity
property float scale_0        ← log-space scale, axis 0
property float scale_1
property float scale_2
property float rot_0          ← quaternion w
property float rot_1          ← quaternion x
property float rot_2          ← quaternion y
property float rot_3          ← quaternion z
end_header
[raw binary data: N × row_size bytes]
```

Our `PlyReader` parses the ASCII header to extract each property's byte offset within
a row, then memory-maps the binary body for random access. `GaussianSplatLoader`
uses `PlyReader` to fill the `RawSplat` CPU struct.

### `RawSplat` CPU/GPU layout (240 bytes)

```cpp
struct RawSplat {
    glm::vec3 position;   // offset   0 — 12 bytes
    float     opacity;    // offset  12 —  4 bytes (pre-sigmoid logit)
    glm::vec4 rotation;   // offset  16 — 16 bytes (wxyz quaternion)
    glm::vec3 scale;      // offset  32 — 12 bytes (pre-exp log-scale)
    float     _pad;       // offset  44 —  4 bytes (alignment padding)
    float     sh[48];     // offset  48 — 192 bytes
    //                                   ─────────
    //                       Total:       240 bytes
};
```

`sh` layout:
- `sh[0]` = R DC (`f_dc_0`), `sh[1]` = G DC, `sh[2]` = B DC
- `sh[3..17]`  = R higher-order l=1,2,3 (`f_rest_0..14`)
- `sh[18..32]` = G higher-order l=1,2,3 (`f_rest_15..29`)
- `sh[33..47]` = B higher-order l=1,2,3 (`f_rest_30..44`)

Because the CPU struct matches the GPU SSBO layout (std430, 60 floats per element),
the splat data is uploaded with a single `memcpy` into a device-local staging buffer.

---

## 4. 3D→2D Gaussian Projection

A 3D Gaussian with covariance Σ₃D projects to a 2D screen-space Gaussian. The
projection is not exact under perspective, but the **EWA (Elliptical Weighted Average)**
approximation using the local Jacobian gives excellent results.

### Step 1: Compute the 3D covariance matrix

```
s  = exp(stored_scale)              (element-wise, so s > 0)
S  = diag(s₀, s₁, s₂)
R  = rotation_matrix(quaternion)
Σ₃D = R · S² · Rᵀ                  (symmetric positive-definite 3×3)
```

### Step 2: Jacobian of the perspective projection

At view-space position **t** = (t_x, t_y, t_z) — where t_z < 0 for objects in front of
the camera — the local linear approximation of the perspective map is:

```
J = ⎡ 1/t_z    0      −t_x / t_z² ⎤
    ⎢  0      1/t_z   −t_y / t_z² ⎥
    ⎣  0       0         0         ⎦
```

The third row is all zeros because we only care about the x,y screen projection; the
resulting product naturally reduces to a 2×2 result when we take the upper-left block.

### Step 3: Project to screen-space covariance

Let W be the upper-left 3×3 of the view matrix (the rotation part only):

```
T    = J · W                        (3×3)
Σ₂D₃ = T · Σ₃D · Tᵀ               (3×3, but only upper-left 2×2 is used)
Σ₂D  = Σ₂D₃[0:2, 0:2] + 0.3·I    (add small regularizer to prevent degenerate ellipses)
```

`Σ₂D` is a 2×2 symmetric positive-definite matrix stored as three floats `[a, b, c]`
representing `[[a, b], [b, c]]`.

### Step 4: Render the quad

Eigendecompose `Σ₂D` to obtain principal axes:

```
λ₁, λ₂  = eigenvalues  (λ₁ ≥ λ₂ > 0)
v₁, v₂  = unit eigenvectors (perpendicular)
r₁ = 3√λ₁,  r₂ = 3√λ₂      (3σ covers 99.7% of Gaussian mass)
```

Each splat is drawn as a 6-vertex (2-triangle) quad whose corners are:

```
corner[i] = center_px + u.x · (r₁ · v₁) + u.y · (r₂ · v₂)
            where u ∈ {(±1, ±1)} for the four corners
```

---

## 5. Spherical Harmonics: View-Dependent Color

Real spherical harmonics (SH) express a function on the unit sphere as a weighted sum
of band-limited basis functions Yₗᵐ(θ, φ), enabling view-dependent effects such as
specular highlights that shift as the camera moves.

### Degrees and coefficients per channel

| Degree l | Basis count | Formula order |
|----------|-------------|---------------|
| 0 (DC)   | 1           | Constant |
| 1        | 3           | Linear in x, y, z |
| 2        | 5           | Quadratic |
| 3        | 7           | Cubic |
| **Total** | **16**     | per R/G/B channel → **48 floats** |

### Evaluation at view direction **d** = (x, y, z)

The view direction is `normalize(splatPos - cameraPos)` in world space (pointing away
from the camera toward the splat).

```glsl
// Hardcoded real SH basis constants (match 3DGS reference implementation)
const float C0 = 0.28209479;
const float C1 = 0.48860251;

float sh_eval(float sh[16], vec3 d) {
    float r = C0 * sh[0]                                       // l=0
      + C1*(-d.y*sh[1] + d.z*sh[2] - d.x*sh[3])               // l=1
      + C2_0*d.x*d.y*sh[4] + C2_1*d.y*d.z*sh[5]               // l=2
      + C2_2*(2*d.z²-d.x²-d.y²)*sh[6]
      + C2_3*d.x*d.z*sh[7] + C2_4*(d.x²-d.y²)*sh[8]
      + /* l=3 terms */ ...;
    return r + 0.5;   // DC offset: training stores color - 0.5
}
```

The `+ 0.5` is a **DC offset** baked in by the 3DGS training code. The network encodes
`(true_color - 0.5)` in the SH coefficients so that the gradients are centered around
zero, which makes optimization more stable. At render time we add 0.5 back and clamp
to [0, 1].

---

## 6. Depth Sorting: Why It Is Required

Gaussian splats are semi-transparent. The **over compositing operator** (α-blending)
requires fragments to be composited **back-to-front** (painter's algorithm):

```
C_out = α_front · C_front  +  (1 − α_front) · C_back
```

Drawing out of order scrambles this formula: a front splat blended on top of an empty
background produces a different result than if a back splat had been drawn first.

**Example:**  Two overlapping splats, A (front, red, α=0.8) and B (back, blue, α=0.5).

| Draw order | Result |
|------------|--------|
| B then A (correct) | 0.8·red + 0.2·(0.5·blue) = mostly red with a hint of blue |
| A then B (wrong)  | 0.5·blue + 0.5·(0.8·red) = half blue, half red — wrong! |

**CPU sort:** Straightforward (`std::sort` on depth), but the sorted index list must be
re-uploaded to the GPU each frame — costly at 3M splats.

**GPU sort (our approach):** The sort runs entirely in compute shaders dispatched in the
same command buffer, before the render pass. Zero host readback.

---

## 7. GPU Radix Sort: How It Works

A **radix sort** processes a 32-bit key in multiple digit passes. We use 8 bits per pass
(256 buckets), so 4 passes cover all 32 bits. Each pass is itself three compute dispatches.

### Why 3 dispatches per pass?

Each workgroup (WG) processes an independent chunk of the array. To produce a globally
sorted output, every WG needs to know where in the output array its elements should
land — but that requires knowing the counts from all other WGs first. The three-phase
structure solves this:

```
Pass k (shift = k × 8 bits):

  Dispatch 1 — Histogram (gsplat_sort_hist.comp)
    Each WG counts occurrences of each 8-bit digit in its chunk.
    Writes histogram[digit * numWG + wgID].

  Dispatch 2 — Prefix sum (gsplat_sort_prefix.comp, single WG)
    Computes the exclusive prefix sum across all WGs for each digit.
    Result: prefix[digit * numWG + wgID] = global output offset for
            (digit d, workgroup w) = total elements with digit < d
                                   + elements with digit d in WGs 0..w-1.

  Dispatch 3 — Scatter (gsplat_sort_scatter.comp)
    Each element reads its digit, atomically increments
    prefix[digit * numWG + wgID], and writes to that output slot.
```

4 passes × 3 dispatches = **12 compute dispatches** per frame, each separated by a
memory barrier.

### Ping-pong buffers

Two key buffers (A and B) and two value (index) buffers (A and B) alternate roles:

```
Preprocess writes: keysA = sort keys, valsA = [0, 1, 2, ..., N-1]

Pass 0 (shift=0,  ping=0): read A → write B
Pass 1 (shift=8,  ping=1): read B → write A
Pass 2 (shift=16, ping=0): read A → write B
Pass 3 (shift=24, ping=1): read B → write A

Final result is always in A.
```

The `ping` flag in the push constant tells each dispatch which pair to read from and
write to, so a single set of four buffers handles all passes.

### Float sort keys for negative view-space Z

View-space Z is **negative** for all visible objects (camera looks down −Z). Raw
IEEE 754 floats don't sort the same way as `uint32_t` when values are negative, because
the sign bit inverts the ordering. The standard fix maps floats to a sortable uint:

```glsl
uint float_to_sort_key(float f) {
    uint bits = floatBitsToUint(f);
    // For negative floats (bit 31 set):  mask = 0xFFFFFFFF → flip all bits
    // For positive floats (bit 31 clear): mask = 0x80000000 → flip sign bit only
    uint mask = uint(-int(bits >> 31)) | 0x80000000u;
    return bits ^ mask;
}
```

After this transform, ascending uint order matches ascending float order for all
finite floats, including negatives. Since more-negative Z = further from camera, sorting
ascending by the transformed key draws furthest splats first — correct back-to-front.

---

## 8. Alpha Compositing: Back-to-Front Over Operator

With splats drawn back-to-front, Vulkan's standard (non-premultiplied) alpha blending
produces the over operator:

```
C_out = α_src · C_src  +  (1 − α_src) · C_dst
```

### Pipeline configuration (`gaussianSplats.cpp`)

```cpp
blendAttach.blendEnable         = VK_TRUE;
blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;

dsState.depthTestEnable  = VK_TRUE;   // occluded by opaque geometry
dsState.depthWriteEnable = VK_FALSE;  // do not corrupt depth buffer
dsState.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
```

### Fragment evaluation (`gsplat.frag`)

```glsl
vec2  d = gl_FragCoord.xy - fragCenter;    // pixel offset from splat center

// 2D Gaussian: power = -0.5 * d^T * Sigma_inv * d
float power = -0.5 * (a*d.x*d.x + 2.0*b*d.x*d.y + c*d.y*d.y);
if (power > 0.0) discard;                  // degenerate / outside ellipse

float g            = exp(power);           // ∈ (0, 1]
float finalAlpha   = fragAlpha * g;        // splat opacity × Gaussian weight
if (finalAlpha < 1.0/255.0) discard;       // cull invisible fragments

outColor = vec4(fragColor, finalAlpha);    // non-premultiplied; blending does the rest
```

The key point: `outColor.rgb` holds the raw (non-premultiplied) color. The GPU blending
unit multiplies by `finalAlpha` automatically via `SRC_ALPHA`.

---

## 9. How This Maps to the Codebase

### File map

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| PLY parser | `src/loaders/plyLoader.h/.cpp` | Header parsing, binary body, property offsets |
| 3DGS loader | `src/loaders/gaussianSplatLoader.h/.cpp` | PLY props → `RawSplat` (240 bytes) |
| GPU system | `src/graphics/gaussianSplats.h/.cpp` | Buffers, pipelines, compute dispatch, draw |
| Preprocess | `shaders/gsplat_preprocess.comp` | 3D→2D projection, SH eval, depth sort keys |
| Histogram  | `shaders/gsplat_sort_hist.comp` | Digit counts per workgroup |
| Prefix sum | `shaders/gsplat_sort_prefix.comp` | Global output offsets per (digit, wg) pair |
| Scatter    | `shaders/gsplat_sort_scatter.comp` | Place sorted elements in output buffer |
| Vertex shader | `shaders/gsplat.vert` | Quad corners via eigendecomposition of Σ₂D |
| Fragment shader | `shaders/gsplat.frag` | Gaussian falloff, alpha blend output |
| Renderer integration | `src/graphics/renderer.h/.cpp` | Calls dispatch_compute + record_draw |
| UI | `src/app.h/.cpp` | "Import Gaussian Splat (.ply)..." menu item |
| Lesson plan | `docs/gaussian_splats.md` | This document |

### GPU buffer summary

| Buffer | Size | Contents |
|--------|------|----------|
| `splatBuffer_`     | N × 240 bytes | `RawSplat` array (position, rot, scale, opacity, SH) |
| `preprocessBuf_`  | N × 40 bytes  | 10 floats: center(2), cov2d(3), color(3), alpha, clipDepth |
| `sortKeysA/B_`    | N × 4 bytes   | Ping-pong float-as-uint depth sort keys |
| `sortValsA/B_`    | N × 4 bytes   | Ping-pong original splat indices |
| `histogramBuf_`   | 256 × numWG × 4 bytes | Per-workgroup digit counts |
| `prefixBuf_`      | 256 × numWG × 4 bytes | Global scatter offsets |

### Descriptor set layout

**Compute pipelines** (preprocess + all sort passes share one layout):
- Set 0: Frame UBO (`frameSetLayout_` from renderer — view, proj, cameraPos, screen size)
- Set 1: 8 SSBOs (splatBuf, preprocessBuf, keysA, keysB, valsA, valsB, hist, prefix)
- Push constants: `{ uint N, uint shift, uint ping, uint numWG }` (16 bytes)

**Render pipeline** (vertex + fragment):
- Set 0: 2 SSBOs (preprocessBuf at binding 0, sortValsA at binding 1)
- Push constants: `{ uint N, float screenW, float screenH }` (12 bytes)

### Frame flow

```
begin_frame()
  ├── 1. Depth prepass (opaque geometry)
  ├── 2. Barrier: depth attachment → shader-read
  ├── 3. Light culling compute (light_cull.comp)
  ├── 4. Barrier: compute writes → fragment reads + depth back to attachment
  ├── 4b. GaussianSplatSystem::dispatch_compute()
  │     ├── gsplat_preprocess.comp          (1 dispatch: project, SH, write keysA/valsA)
  │     ├── [barrier]
  │     └── 4× radix sort pass:
  │           ├── gsplat_sort_hist.comp     (numWG dispatches)
  │           ├── [barrier]
  │           ├── gsplat_sort_prefix.comp   (1 dispatch)
  │           ├── [barrier]
  │           ├── gsplat_sort_scatter.comp  (numWG dispatches)
  │           └── [barrier]
  ├── 5. Barrier: compute writes → vertex shader reads
  └── vkCmdBeginRenderPass
        └── draw_scene()
              ├── PBR meshes
              ├── heatmap debug overlay
              ├── debug light wireframes
              └── GaussianSplatSystem::record_draw()
                    └── gsplat.vert + gsplat.frag  (N×6 vertices, alpha-blended)
```

### Important implementation notes

- **Sort result always in A.** After 4 passes the result is always in `sortValsA_` because
  we start from A and alternate: A→B→A→B→A. The render descriptor set is wired
  statically to `sortValsA_`.

- **Depth test ON, write OFF.** Splats are depth-tested against opaque geometry so they
  are properly occluded, but they don't write to the depth buffer (which would prevent
  other splats behind them from rendering).

- **NDC depth from preprocess.** The clip-space depth for each splat (`clipPos.z / clipPos.w`)
  is computed once in the preprocess shader and stored in `preprocessBuf_` (index 9 of
  the 10 floats per splat). The vertex shader reads this value and places it directly into
  `gl_Position.z` so the hardware depth test works correctly.

- **Shader pack key naming.** All six new shaders are compiled with `glslc` and packed into
  `assets.pak` under keys `shaders/gsplat_*.spv`. `GaussianSplatSystem::init()` receives
  a `pak::PackFile&` reference and loads them on first call.

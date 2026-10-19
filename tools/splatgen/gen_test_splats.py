#!/usr/bin/env python3
"""Generate small synthetic 3D Gaussian Splatting test scenes (.ply).

Usage: python gen_test_splats.py [-h] [--out DIR]

Writes standard 3DGS binary PLY files (same properties as the reference
gaussian-splatting trainer output) into DIR (default: models/splats):

  test_axes.ply      Elongated splats along +X (red), +Y (green), +Z (blue)
                     plus a white sphere at the origin. Checks quaternion
                     handling and anisotropic covariance projection.
  test_sh_sphere.ply Fibonacci sphere whose colour changes with view
                     direction through degree-1 SH. Checks SH evaluation.
  test_layers.ply    Three overlapping translucent shells (red outside,
                     green, blue core). Checks depth sorting and blending.

Scenes are authored Y-up, then stored in the COLMAP convention used by real
captures (Y down, Z forward) by rotating 180 degrees about X. The editor's
default splat import transform undoes that, so every sample appears upright.

Only the Python standard library is used. Output is deterministic.
"""

import argparse
import math
import random
import struct
import sys
from pathlib import Path

SH_C0 = 0.28209479177387814
N_REST = 45  # degree 1..3, 15 coefficients per channel


def logit(a):
    a = min(max(a, 1e-4), 1.0 - 1e-4)
    return math.log(a / (1.0 - a))


def rgb_to_dc(rgb):
    return [(c - 0.5) / SH_C0 for c in rgb]


def quat_mul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    )


def quat_from_axis_angle(axis, angle):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z)
    s = math.sin(angle * 0.5) / n
    return (math.cos(angle * 0.5), x * s, y * s, z * s)


def quat_x_to(direction):
    """Rotation taking local +X onto `direction` (unit vector)."""
    dx, dy, dz = direction
    dot = dx  # dot((1,0,0), d)
    if dot > 0.9999:
        return (1.0, 0.0, 0.0, 0.0)
    if dot < -0.9999:
        return (0.0, 0.0, 1.0, 0.0)
    axis = (0.0, -dz, dy)  # cross((1,0,0), d)
    return quat_from_axis_angle(axis, math.acos(dot))


# 180 degrees about X: Y-up authoring space -> COLMAP (Y down, Z forward)
TO_COLMAP = (0.0, 1.0, 0.0, 0.0)


def splat(pos, scale, rot, rgb, alpha, rest=None):
    """One Gaussian in authoring space. scale is linear, rot is wxyz."""
    x, y, z = pos
    p = (x, -y, -z)  # apply TO_COLMAP to the position
    q = quat_mul(TO_COLMAP, rot)
    return {
        "pos": p,
        "dc": rgb_to_dc(rgb),
        "rest": rest if rest is not None else [0.0] * N_REST,
        "opacity": logit(alpha),
        "scale": [math.log(s) for s in scale],
        "rot": q,
    }


def write_ply(path, splats):
    props = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    props += [f"f_rest_{i}" for i in range(N_REST)]
    props += ["opacity", "scale_0", "scale_1", "scale_2"]
    props += ["rot_0", "rot_1", "rot_2", "rot_3"]

    header = "ply\nformat binary_little_endian 1.0\n"
    header += f"element vertex {len(splats)}\n"
    header += "".join(f"property float {p}\n" for p in props)
    header += "end_header\n"

    row = struct.Struct("<" + "f" * len(props))
    with open(path, "wb") as f:
        f.write(header.encode("ascii"))
        for s in splats:
            values = list(s["pos"]) + [0.0, 0.0, 0.0] + s["dc"] + s["rest"]
            values += [s["opacity"]] + s["scale"] + list(s["rot"])
            f.write(row.pack(*values))
    print(f"wrote {path} ({len(splats)} splats)")


def fibonacci_sphere(n, radius):
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(n):
        y = 1.0 - 2.0 * (i + 0.5) / n
        r = math.sqrt(max(0.0, 1.0 - y * y))
        phi = golden * i
        yield (math.cos(phi) * r * radius, y * radius, math.sin(phi) * r * radius)


def scene_axes():
    out = []
    axes = [((1, 0, 0), (1.0, 0.15, 0.15)),
            ((0, 1, 0), (0.15, 1.0, 0.15)),
            ((0, 0, 1), (0.2, 0.35, 1.0))]
    for direction, rgb in axes:
        rot = quat_x_to(direction)
        for k in range(12):
            t = 0.25 + k * 0.16
            pos = tuple(d * t for d in direction)
            # long along local X, thin across
            out.append(splat(pos, (0.09, 0.02, 0.02), rot, rgb, 0.95))
        tip = tuple(d * 2.3 for d in direction)
        out.append(splat(tip, (0.08, 0.08, 0.08), (1, 0, 0, 0), rgb, 0.95))
    for p in fibonacci_sphere(400, 0.18):
        out.append(splat(p, (0.035, 0.035, 0.035), (1, 0, 0, 0),
                         (0.95, 0.95, 0.95), 0.9))
    return out


def scene_sh_sphere():
    out = []
    rng = random.Random(1234)
    for p in fibonacci_sphere(6000, 1.0):
        n = p  # radius 1: position is the normal
        # Grey base tinted by normal, plus degree-1 SH so each channel
        # brightens along a different view axis. Orbiting the sphere should
        # visibly shift its colour; a constant colour means SH is ignored.
        base = (0.45 + 0.1 * n[0], 0.45 + 0.1 * n[1], 0.45 + 0.1 * n[2])
        rest = [0.0] * N_REST
        k = 0.9
        # Per-channel degree-1 basis order is (-y, +z, -x) * C1.
        rest[0 * 15 + 2] = -k  # R follows view x
        rest[1 * 15 + 0] = -k  # G follows view y
        rest[2 * 15 + 1] = k   # B follows view z
        rot = quat_from_axis_angle((rng.random(), rng.random(), rng.random() + 0.1),
                                   rng.random() * math.pi)
        out.append(splat(p, (0.045, 0.045, 0.012), rot, base, 0.9, rest))
    return out


def scene_layers():
    out = []
    shells = [(1.2, (0.9, 0.2, 0.2), 0.35, 1500),
              (0.8, (0.2, 0.9, 0.2), 0.5, 900),
              (0.4, (0.2, 0.3, 0.95), 0.9, 400)]
    for radius, rgb, alpha, count in shells:
        size = radius * 0.09
        for p in fibonacci_sphere(count, radius):
            # flat disc: thin along local X, which points along the normal
            out.append(splat(p, (size * 0.4, size, size), quat_x_to(
                tuple(c / radius for c in p)), rgb, alpha))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(Path(__file__).resolve().parents[2] / "models" / "splats"),
                    help="output directory (default: models/splats)")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    write_ply(out / "test_axes.ply", scene_axes())
    write_ply(out / "test_sh_sphere.ply", scene_sh_sphere())
    write_ply(out / "test_layers.ply", scene_layers())
    return 0


if __name__ == "__main__":
    sys.exit(main())

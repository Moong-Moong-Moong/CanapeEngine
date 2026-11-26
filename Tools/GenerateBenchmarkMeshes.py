"""Generate the ignored glTF meshes used by RenderingBenchmarkController.

The output stays under Game/Content/Models, following the project's existing
model asset and Git ignore rules. Run this script after a fresh checkout.
"""

import json
import math
import struct
from pathlib import Path


OUTPUT = Path(__file__).resolve().parents[1] / "Game/Content/Models/RenderingBenchmark"


def write_mesh(name, positions, normals, uvs, primitive_indices):
    binary = bytearray()
    views = []
    accessors = []

    def accessor(values, fmt, components, component_type, kind, target):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        for value in values:
            binary.extend(struct.pack(fmt, *value) if isinstance(value, tuple) else struct.pack(fmt, value))
        view = len(views)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset, "target": target})
        result = {"bufferView": view, "componentType": component_type, "count": len(values), "type": kind}
        if kind == "VEC3" and values:
            result["min"] = [min(v[i] for v in values) for i in range(3)]
            result["max"] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(result)
        return len(accessors) - 1

    pos = accessor(positions, "<3f", 3, 5126, "VEC3", 34962)
    nor = accessor(normals, "<3f", 3, 5126, "VEC3", 34962)
    uv = accessor(uvs, "<2f", 2, 5126, "VEC2", 34962)
    primitives = []
    for indices in primitive_indices:
        idx = accessor(indices, "<I", 1, 5125, "SCALAR", 34963)
        primitives.append({"attributes": {"POSITION": pos, "NORMAL": nor, "TEXCOORD_0": uv},
                           "indices": idx, "mode": 4})

    document = {
        "asset": {"version": "2.0", "generator": "Canape GenerateBenchmarkMeshes"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": primitives}], "materials": [],
        "buffers": [{"uri": name + ".bin", "byteLength": len(binary)}],
        "bufferViews": views, "accessors": accessors,
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / (name + ".gltf")).write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
    (OUTPUT / (name + ".bin")).write_bytes(binary)


def cube():
    positions, normals, uvs, triangles = [], [], [], []
    faces = [
        ((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)),
        ((0, 1, 0), (0, 0, 1)), ((0, -1, 0), (0, 0, -1)),
        ((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0)),
    ]
    for normal, up in faces:
        right = (normal[1] * up[2] - normal[2] * up[1],
                 normal[2] * up[0] - normal[0] * up[2],
                 normal[0] * up[1] - normal[1] * up[0])
        base = len(positions)
        for rs, us, tex in [(-1, 1, (0., 0.)), (1, 1, (1., 0.)),
                            (1, -1, (1., 1.)), (-1, -1, (0., 1.))]:
            positions.append(tuple(0.5 * (normal[i] + rs * right[i] + us * up[i]) for i in range(3)))
            normals.append(tuple(float(v) for v in normal))
            uvs.append(tex)
        triangles.extend([(base, base + 1, base + 2), (base, base + 2, base + 3)])
    for parts in (2, 4, 8):
        groups = []
        for part in range(parts):
            selected = triangles[part * len(triangles) // parts:(part + 1) * len(triangles) // parts]
            groups.append([index for tri in selected for index in tri])
        write_mesh(f"CubeSplit{parts}", positions, normals, uvs, groups)


def sphere(name, segments, rings):
    positions, normals, uvs, indices = [], [], [], []
    for ring in range(rings + 1):
        v = ring / rings
        theta = math.pi * v
        for segment in range(segments + 1):
            u = segment / segments
            phi = 2.0 * math.pi * u
            normal = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            positions.append(tuple(0.5 * value for value in normal))
            normals.append(normal)
            uvs.append((u, v))
    stride = segments + 1
    for ring in range(rings):
        for segment in range(segments):
            a = ring * stride + segment
            b = a + 1
            c = a + stride
            d = c + 1
            if ring != 0:
                indices.extend((a, b, c))
            if ring != rings - 1:
                indices.extend((b, d, c))
    assert len(indices) // 3 == 2 * segments * (rings - 1)
    write_mesh(name, positions, normals, uvs, [indices])


if __name__ == "__main__":
    cube()
    for name, segments, rings in [
        ("Sphere1024", 32, 17), ("Sphere10000", 100, 51),
        ("Sphere50000", 200, 126), ("Sphere100000", 250, 201),
    ]:
        sphere(name, segments, rings)
    print(f"Generated benchmark meshes in {OUTPUT}")

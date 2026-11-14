#!/usr/bin/env python3
"""Generate the sample's editable glTF vehicle meshes and their distant silhouettes."""
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1] / "models"
TEAM = (1, 1, 1, 1)
ARMOR = (0.38, 0.46, 0.54, 0)
STEEL = (0.78, 0.84, 0.86, 0)
TRACK = (0.10, 0.13, 0.15, 0)
GREEN = (0.58, 0.88, 0.29, 0)


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.colors = [], [], []

    def triangle(self, a, b, c, color):
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        normal = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        length = math.sqrt(sum(x*x for x in normal))
        normal = [x / length for x in normal]
        for position in (a, b, c):
            self.positions.append(position)
            self.normals.append(normal)
            self.colors.append(color)

    def quad(self, a, b, c, d, color):
        self.triangle(a, b, c, color)
        self.triangle(a, c, d, color)

    def box(self, x, y, z, rx, height, rz, color):
        self.quad((x-rx,y+height,z-rz), (x-rx,y+height,z+rz), (x+rx,y+height,z+rz), (x+rx,y+height,z-rz), color)
        self.quad((x-rx,y,z+rz), (x+rx,y,z+rz), (x+rx,y+height,z+rz), (x-rx,y+height,z+rz), color)
        self.quad((x-rx,y,z-rz), (x-rx,y,z+rz), (x-rx,y+height,z+rz), (x-rx,y+height,z-rz), color)
        self.quad((x+rx,y,z+rz), (x+rx,y,z-rz), (x+rx,y+height,z-rz), (x+rx,y+height,z+rz), color)
        self.quad((x+rx,y,z-rz), (x-rx,y,z-rz), (x-rx,y+height,z-rz), (x+rx,y+height,z-rz), color)

    def pyramid(self, radius, height, color):
        for a, b in [((-radius,0,0),(0,0,radius)), ((0,0,radius),(radius,0,0)),
                     ((radius,0,0),(0,0,-radius)), ((0,0,-radius),(-radius,0,0))]:
            self.triangle(a, b, (0,height,0), color)


def vehicle(kind):
    mesh = Mesh()
    if kind == "scout":
        mesh.pyramid(2.6, 2.8, ARMOR)
        mesh.box(0, 2.4, 0, 0.8, 0.5, 0.9, TEAM)
        mesh.box(0, 1, -2.1, 0.2, 0.3, 1.2, STEEL)
    elif kind == "support":
        mesh.pyramid(3.1, 4, TEAM)
        mesh.box(0, 4, 0, 0.5, 0.5, 2, GREEN)
        mesh.box(0, 4, 0, 2, 0.5, 0.5, GREEN)
    else:
        radius = 3.7 if kind == "siege" else 3
        for sign in (-1, 1):
            mesh.box(sign*radius*0.85, 0, 0, radius*0.25, 1.3, radius*1.25, TRACK)
        mesh.box(0, 0.5, 0, radius*0.8, 1.3, radius, ARMOR)
        mesh.box(0, 1.8, 0, radius*0.62, 1.1, radius*0.65, TEAM)
        mesh.box(0, 2.5, -3.5 if kind == "siege" else -2.4, 0.38, 0.45,
                 3.5 if kind == "siege" else 2.4, STEEL)
    return mesh


def export(kind, close, distant):
    blob = bytearray()
    document = {"asset": {"version": "2.0", "generator": "MoonBase tools/generate_models.py"},
                "scene": 0, "scenes": [{"nodes": [0, 1]}],
                "nodes": [{"name": "close", "mesh": 0}, {"name": "distant", "mesh": 1}],
                "meshes": [], "materials": [{"name": "vehicle", "pbrMetallicRoughness":
                    {"metallicFactor": 0, "roughnessFactor": 1}}],
                "bufferViews": [], "accessors": []}

    def accessor(values, components, minimum=False):
        start = len(blob)
        for value in values:
            blob.extend(struct.pack("<" + "f"*components, *value))
        view = len(document["bufferViews"])
        document["bufferViews"].append({"buffer": 0, "byteOffset": start, "byteLength": len(blob)-start, "target": 34962})
        item = {"bufferView": view, "componentType": 5126, "count": len(values), "type": f"VEC{components}"}
        if minimum:
            item["min"] = [min(v[i] for v in values) for i in range(components)]
            item["max"] = [max(v[i] for v in values) for i in range(components)]
        index = len(document["accessors"])
        document["accessors"].append(item)
        return index

    for name, mesh in (("close", close), ("distant", distant)):
        attributes = {"POSITION": accessor(mesh.positions, 3, True), "NORMAL": accessor(mesh.normals, 3),
                      "COLOR_0": accessor(mesh.colors, 4)}
        document["meshes"].append({"name": name, "primitives": [{"attributes": attributes, "material": 0}]})
    document["buffers"] = [{"uri": f"{kind}.bin", "byteLength": len(blob)}]
    (ROOT / f"{kind}.bin").write_bytes(blob)
    (ROOT / f"{kind}.gltf").write_text(json.dumps(document, indent=2) + "\n")
    for index, suffix in enumerate(("", "_distant")):
        (ROOT / f"{kind}{suffix}.model").write_text(
            f'mesh: "/models/{kind}.gltf"\nmesh_index: {index}\nmesh_name: "{"close" if index == 0 else "distant"}"\n'
            'materials { name: "vehicle" material: "/render/unit.material" }\ncreate_go_bones: false\n')


def main():
    ROOT.mkdir(exist_ok=True)
    kinds = ("scout", "assault", "siege", "support")
    for kind, radius, height in zip(kinds, (2.6, 3, 3.7, 3.1), (2.5, 2.5, 4.5, 3.5)):
        distant = Mesh()
        distant.pyramid(radius*1.35, height, TEAM)
        export(kind, vehicle(kind), distant)
    # A factory holds the normal model dependencies without creating any unit objects.
    (ROOT / "preload.factory").write_text('prototype: "/models/preload.go"\nload_dynamically: false\n')
    components = []
    for kind in kinds:
        for suffix in ("", "_distant"):
            name = kind + suffix
            components.append(f'components {{ id: "{name}" component: "/models/{name}.model" }}')
    (ROOT / "preload.go").write_text("\n".join(components) + "\n")


if __name__ == "__main__":
    main()

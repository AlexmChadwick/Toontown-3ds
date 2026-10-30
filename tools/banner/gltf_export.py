"""Extract Panda3D geometry into a binary glTF 2.0 (.glb) for pycgfx."""

from __future__ import annotations

import json
import math
import struct
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Iterable

import numpy as np
from panda3d.core import Geom, GeomNode, GeomTriangles, GeomVertexReader, InternalName, NodePath, TextureAttrib, TextureStage
from PIL import Image


@dataclass
class MeshPart:
    name: str
    positions: np.ndarray  # (N, 3) float32
    normals: np.ndarray  # (N, 3) float32
    uvs: np.ndarray  # (N, 2) float32
    indices: np.ndarray  # (T,) uint32
    base_color: np.ndarray  # RGBA uint8 image
    double_sided: bool = True


@dataclass
class SceneObject:
    mesh: MeshPart
    translation: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rotation_euler: tuple[float, float, float] = (0.0, 0.0, 0.0)  # degrees XYZ
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)


def _read_geom(geom: Geom, world_mat) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray] | None:
    geom = geom.decompose()
    vdata = geom.getVertexData()
    if vdata.getNumRows() == 0:
        return None
    tris = geom.getPrimitive(0)
    if not isinstance(tris, GeomTriangles):
        return None

    pos_r = GeomVertexReader(vdata, InternalName.get_vertex())
    nrm_r = GeomVertexReader(vdata, InternalName.get_normal())
    uv_r = GeomVertexReader(vdata, InternalName.get_texcoord())

    positions = []
    normals = []
    uvs = []
    for i in range(vdata.getNumRows()):
        pos_r.set_row(i)
        p = pos_r.get_data3()
        p4 = world_mat.xform_point(p)
        positions.append((p4.x, p4.y, p4.z))
        if nrm_r.has_column():
            nrm_r.set_row(i)
            n = nrm_r.get_data3()
            n4 = world_mat.xform_vec(n)
            n4.normalize()
            normals.append((n4.x, n4.y, n4.z))
        else:
            normals.append((0.0, 0.0, 1.0))
        if uv_r.has_column():
            uv_r.set_row(i)
            t = uv_r.get_data2()
            uvs.append((t.x, t.y))
        else:
            uvs.append((0.0, 0.0))

    indices: list[int] = []
    for pi in range(geom.getNumPrimitives()):
        pr = geom.getPrimitive(pi)
        for i in range(pr.getNumVertices()):
            indices.append(pr.getVertex(i))

    return (
        np.asarray(positions, dtype=np.float32),
        np.asarray(normals, dtype=np.float32),
        np.asarray(uvs, dtype=np.float32),
        np.asarray(indices, dtype=np.uint32),
    )


def _texture_from_state(state, *, max_dim: int = 96) -> np.ndarray:
    ta = state.getAttrib(TextureAttrib)
    if ta is None:
        return np.full((4, 4, 4), 255, dtype=np.uint8)
    tex = ta.getOnTexture(TextureStage.get_default())
    if tex is None:
        return np.full((4, 4, 4), 255, dtype=np.uint8)
    img = tex.get_ram_image_as("RGBA")
    w, h = tex.get_x_size(), tex.get_y_size()
    arr = np.frombuffer(img, dtype=np.uint8).reshape(h, w, 4)
    arr = np.flipud(arr.copy())
    img2 = Image.fromarray(arr, "RGBA")
    img2.thumbnail((max_dim, max_dim), Image.Resampling.LANCZOS)
    return np.asarray(img2)


def collect_meshes(root: NodePath, name_prefix: str = "mesh") -> list[MeshPart]:
    parts: list[MeshPart] = []
    geoms = root.findAllMatches("**/+GeomNode")
    for gi in range(geoms.getNumPaths()):
        gnp = geoms.getPath(gi)
        world = gnp.getNetTransform().getMat()
        gn: GeomNode = gnp.node()
        for idx in range(gn.getNumGeoms()):
            geom = gn.getGeom(idx)
            state = gn.getGeomState(idx)
            data = _read_geom(geom, world)
            if data is None:
                continue
            pos, nrm, uv, ind = data
            if len(ind) < 3:
                continue
            tex = _texture_from_state(state)
            parts.append(
                MeshPart(
                    name=f"{name_prefix}_{gi}_{idx}",
                    positions=pos,
                    normals=nrm,
                    uvs=uv,
                    indices=ind,
                    base_color=tex,
                )
            )
    return parts


def make_textured_quad(
    name: str,
    width: float,
    height: float,
    rgba: np.ndarray,
    *,
    double_sided: bool = True,
) -> MeshPart:
    hw, hh = width * 0.5, height * 0.5
    positions = np.array(
        [[-hw, -hh, 0.0], [hw, -hh, 0.0], [hw, hh, 0.0], [-hw, hh, 0.0]], dtype=np.float32
    )
    normals = np.tile(np.array([[0.0, 0.0, 1.0]], dtype=np.float32), (4, 1))
    uvs = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]], dtype=np.float32)
    indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
    return MeshPart(
        name=name,
        positions=positions,
        normals=normals,
        uvs=uvs,
        indices=indices,
        base_color=rgba,
        double_sided=double_sided,
    )


def _pack_glb(chunks: Iterable[tuple[bytes, bytes]]) -> bytes:
    body = b""
    for chunk_type, data in chunks:
        pad = b"\x00" if len(data) % 2 else b""
        body += struct.pack("<I4s", len(data) + len(pad), chunk_type) + data + pad
    header = struct.pack("<4sII", b"glTF", 2, 12 + len(body))
    return header + body


def write_glb(objects: list[SceneObject], out_path: Path) -> None:
    """Write a minimal glTF 2.0 binary with one material/texture per mesh."""
    buffer_parts: list[bytes] = []
    accessors = []
    buffer_views = []
    meshes_json = []
    nodes_json = []
    materials_json = []
    textures_json = []
    images_json = []
    byte_offset = 0

    def append_data(data: bytes, target: int | None = None) -> int:
        nonlocal byte_offset
        off = byte_offset
        buffer_parts.append(data)
        pad = (4 - (len(data) % 4)) % 4
        if pad:
            buffer_parts.append(b"\x00" * pad)
            byte_offset += pad
        bv = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target is not None:
            bv["target"] = target
        buffer_views.append(bv)
        byte_offset += len(data)
        return off

    for obj_idx, obj in enumerate(objects):
        mesh = obj.mesh
        pos_bytes = mesh.positions.tobytes()
        nrm_bytes = mesh.normals.tobytes()
        uv_bytes = mesh.uvs.astype(np.float32).tobytes()
        idx_bytes = mesh.indices.astype(np.uint32).tobytes()

        pos_off = append_data(pos_bytes, 34962)
        nrm_off = append_data(nrm_bytes, 34962)
        uv_off = append_data(uv_bytes, 34962)
        idx_off = append_data(idx_bytes, 34963)

        pos_acc = len(accessors)
        accessors.append(
            {
                "bufferView": len(buffer_views) - 4,
                "componentType": 5126,
                "count": len(mesh.positions),
                "type": "VEC3",
                "min": mesh.positions.min(axis=0).tolist(),
                "max": mesh.positions.max(axis=0).tolist(),
            }
        )
        nrm_acc = len(accessors)
        accessors.append(
            {
                "bufferView": len(buffer_views) - 3,
                "componentType": 5126,
                "count": len(mesh.normals),
                "type": "VEC3",
            }
        )
        uv_acc = len(accessors)
        accessors.append(
            {
                "bufferView": len(buffer_views) - 2,
                "componentType": 5126,
                "count": len(mesh.uvs),
                "type": "VEC2",
            }
        )
        idx_acc = len(accessors)
        accessors.append(
            {
                "bufferView": len(buffer_views) - 1,
                "componentType": 5125,
                "count": len(mesh.indices),
                "type": "SCALAR",
            }
        )

        img = Image.fromarray(mesh.base_color, "RGBA")
        png = BytesIO()
        img.save(png, format="PNG")
        png_bytes = png.getvalue()
        img_off = append_data(png_bytes)
        images_json.append({"bufferView": len(buffer_views) - 1, "mimeType": "image/png"})
        textures_json.append({"source": len(images_json) - 1})
        mat_idx = len(materials_json)
        materials_json.append(
            {
                "name": mesh.name,
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": len(textures_json) - 1},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.65,
                },
                "alphaMode": "BLEND" if mesh.base_color[:, :, 3].min() < 255 else "OPAQUE",
                "doubleSided": mesh.double_sided,
            }
        )

        meshes_json.append(
            {
                "name": mesh.name,
                "primitives": [
                    {
                        "attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc, "TEXCOORD_0": uv_acc},
                        "indices": idx_acc,
                        "material": mat_idx,
                    }
                ],
            }
        )

        rx, ry, rz = obj.rotation_euler
        nodes_json.append(
            {
                "name": mesh.name,
                "mesh": len(meshes_json) - 1,
                "translation": list(obj.translation),
                "rotation": _euler_deg_to_quat(rx, ry, rz),
                "scale": list(obj.scale),
            }
        )

    bin_blob = b"".join(buffer_parts)
    json_obj = {
        "asset": {"version": "2.0", "generator": "toontown3ds-banner"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes_json)))}],
        "nodes": nodes_json,
        "meshes": meshes_json,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "materials": materials_json,
        "textures": textures_json,
        "images": images_json,
        "buffers": [{"byteLength": len(bin_blob)}],
    }
    json_bytes = json.dumps(json_obj, separators=(",", ":")).encode("utf-8")
    json_pad = (4 - (len(json_bytes) % 4)) % 4
    json_bytes += b" " * json_pad
    glb = _pack_glb([(b"JSON", json_bytes), (b"BIN\x00", bin_blob)])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(glb)


def _euler_deg_to_quat(rx: float, ry: float, rz: float) -> list[float]:
    rx, ry, rz = map(math.radians, (rx, ry, rz))
    cx, sx = math.cos(rx / 2), math.sin(rx / 2)
    cy, sy = math.cos(ry / 2), math.sin(ry / 2)
    cz, sz = math.cos(rz / 2), math.sin(rz / 2)
    qw = cx * cy * cz + sx * sy * sz
    qx = sx * cy * cz - cx * sy * sz
    qy = cx * sy * cz + sx * cy * sz
    qz = cx * cy * sz - sx * sy * cz
    return [qx, qy, qz, qw]

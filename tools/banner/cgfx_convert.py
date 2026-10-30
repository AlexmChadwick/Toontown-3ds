#!/usr/bin/env python3
"""glTF to CGFX for the HOME Menu banner.

pycgfx is cloned under third_party/ and gitignored, so the patches live here
and are applied in memory:

- RGBA8 for the logo and the dog (tex3ds byte order A,B,G,R). The backdrop
  stays RGBA4. pycgfx's swizzle had no RGBA8 case.
- Textures are refused if a side is over 256 or not a multiple of 8. pycgfx
  otherwise resizes each axis on its own and stretches the image.
- Specular is forced to black. Roughness 1 still leaves a 0.1 specular add.
- The logo bone (``name``) is Y-axial billboard, with its transform already
  baked into the vertices.
- Mipmaps stay at one level. A second level of the 256x256 RGBA8 logo is
  64 KiB, and the logo, dog and backdrop together would pass 512 KiB.
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PYCGFX = HERE / "third_party" / "pycgfx"

RGBA8_NAMES = {"name", "logo", "toon"}


def _install_patches() -> object:
    if not (PYCGFX / "main.py").is_file():
        raise SystemExit(f"missing {PYCGFX} (tools/banner/build.sh clones it)")
    sys.path.insert(0, str(PYCGFX))
    pycgfx_main = importlib.import_module("main")
    swizzler = importlib.import_module("cgfx.swizzler")
    txob = importlib.import_module("cgfx.txob")

    source = Path(swizzler.__file__).read_text()
    needle = "                        case _:\n"
    insert = (
        "                        case TextureFormat.RGBA8:\n"
        "                            pass\n"
        "                        case _:\n"
    )
    if needle not in source:
        raise SystemExit("pycgfx swizzle() has no default case to patch")
    start = source.index("def swizzle(")
    end = source.index("def to_txob(")
    patched = source[start:end].replace(needle, insert, 1)
    if "TextureFormat.RGBA8" not in patched:
        raise SystemExit("RGBA8 case was not inserted")
    exec(patched, swizzler.__dict__)

    texture_format = txob.TextureFormat
    orig_to_txob = swizzler.to_txob

    def to_txob(im, format=texture_format.RGBA4, mipmaps=1):
        if im.width > 256 or im.height > 256 or im.width % 8 or im.height % 8:
            raise SystemExit(
                f"refusing {im.width}x{im.height} texture; "
                "pycgfx would clamp each side to 256 on its own"
            )
        if mipmaps != 1:
            raise SystemExit("extra mip levels do not fit in 512 KiB")
        return orig_to_txob(im, format, mipmaps)

    swizzler.to_txob = to_txob

    def gltf_get_texture(cgfx, gltf, image_id, normal=False):
        from io import BytesIO

        from PIL import Image

        image = gltf.model.images[image_id]
        tex_name = image.name or image.uri or f"image{image_id}"
        if normal:
            tex_name = f"NORM~{tex_name}"
        if tex_name in cgfx.data.textures:
            return cgfx.data.textures[tex_name]
        if image.uri is not None:
            image_data = gltf.get_resource(image.uri).data
        elif image.bufferView is not None:
            image_data = pycgfx_main.gltf_get_bv_data(gltf, image.bufferView)
        else:
            raise SystemExit(f"{tex_name} has no image data")
        im = Image.open(BytesIO(image_data))
        if im.width > 256 or im.height > 256:
            raise SystemExit(
                f"{tex_name} is {im.width}x{im.height}; resize it uniformly "
                "before export (pycgfx would squash each axis)"
            )
        fmt = (
            texture_format.RGBA8
            if tex_name in RGBA8_NAMES
            else texture_format.RGBA4
        )
        txob_img = swizzler.to_txob(
            im.transpose(Image.Transpose.FLIP_TOP_BOTTOM), fmt
        )
        txob_img.name = tex_name
        cgfx.data.textures.add(tex_name, txob_img)
        return txob_img

    pycgfx_main.gltf_get_texture = gltf_get_texture
    return pycgfx_main


def _finish(cgfx) -> None:
    sobj = importlib.import_module("cgfx.sobj")
    shared = importlib.import_module("cgfx.shared")
    cmdl = cgfx.data.models["COMMON"]
    if cmdl is None or cmdl.name != "COMMON":
        raise SystemExit("CMDL is not named COMMON")
    logo = cmdl.skeleton.bones["name"]
    if logo is None:
        raise SystemExit("logo bone 'name' is missing")
    logo.billboard_mode = sobj.BillboardMode.YAxial
    # Identity values stay. Drop the IsIdentity flag so the billboard runs.
    logo.flags &= ~sobj.BoneFlag.IsIdentity
    for bone_name in cmdl.skeleton.bones:
        bone = cmdl.skeleton.bones[bone_name]
        if bone.flags & sobj.BoneFlag.HasSkinningMatrix:
            raise SystemExit(f"{bone_name} is skinned; HOME Menu would freeze")
    for mat_name in cmdl.materials:
        mat = cmdl.materials[mat_name]
        mat.material_color.specular[0] = shared.ColorFloat(0, 0, 0, 0)
        mat.material_color.specular[1] = shared.ColorFloat(0, 0, 0, 0)
        mat.material_color.constant[0] = shared.ColorFloat(0, 0, 0, 1)
    if "COMMON" not in list(cgfx.data.skeletal_animations):
        raise SystemExit("CANM is not named COMMON")
    anim = cgfx.data.skeletal_animations["COMMON"]
    print(f"CMDL COMMON  CANM COMMON  frame_size {anim.frame_size}")
    print(f"logo billboard {logo.billboard_mode.name}")
    for tex_name in cgfx.data.textures:
        tex = cgfx.data.textures[tex_name]
        print(
            f"tex {tex_name} {tex.width}x{tex.height} "
            f"{tex.hw_format.name} mips {tex.mipmap_level_count}"
        )


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} in.glb out.cgfx")
    out_path = Path(sys.argv[2])
    pycgfx_main = _install_patches()
    gltf = pycgfx_main.gltflib.GLTF.load(sys.argv[1], load_file_resources=True)
    cgfx = pycgfx_main.convert_gltf(gltf)
    _finish(cgfx)
    data = pycgfx_main.write(cgfx)
    if len(data) > 524288:
        raise SystemExit(f"banner.cgfx is {len(data)} bytes (>512 KiB)")
    out_path.write_bytes(data)
    print(f"wrote {out_path} ({len(data)} bytes)")


if __name__ == "__main__":
    main()

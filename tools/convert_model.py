# GLB(glTF)・FBX・OBJ のモデルを、このゲームが読める形(OBJ + MTL + テクスチャ1枚)に変換する、Blender用のスクリプト。
# (最初はGLB用に作ったが、FBX・OBJも、拡張子で、Blenderの読み込みを切り替えて扱える)
#
# 使い方(PowerShell。Blenderの画面は開かない):
#   & "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background --python tools\convert_model.py -- `
#       --in "C:\Users\...\Downloads\xxx.glb" --out-dir Assets\Models\Stores\xxx --name xxx `
#       [--rotate-y 180] [--crop X0 X1 Z0 Z1] [--drop-ground 0.5] [--tile 128] [--size 1.0] `
#       [--title "Mini Market" --author "Elbolillo" --source "https://..." --license "CC BY 4.0"]
#
# なぜ変換が要るか: このゲームのOBJローダーは、モデル1つにつきテクスチャを1枚しか扱えない(材質ごとの色だけが頂点色になる)。
# 市販・配布のモデルは、材質が数十個・テクスチャが数十枚あることが多いので、そのままOBJにすると、見た目が崩れる。
# そこで、全ての材質を、1枚の「アトラス」(小さな正方形のマスを並べた画像)にまとめ、UVを付け替える。
#   ・色だけの材質: そのマスを、単色で塗る(glTFのbaseColorはリニアなので、画面の値(sRGB)に直して塗る)。
#   ・テクスチャ付きの材質: テクスチャを、マスの大きさに縮めて貼る(UVが0〜1に収まる物)。
#   ・UVが0〜1を超えて繰り返している材質(床のタイルなど): アトラスでは繰り返せないので、平均の色の単色にする
#     (街の画面では、建物は小さく見えるので、繰り返しの細かい模様は見えない)。
# その他の処理: 全てのオブジェクトの変形(位置・回転・拡大)を頂点に焼き込む / 範囲の切り出し(--crop) / 地面の削除(--drop-ground) /
#   Y軸の回転(--rotate-y。正面を+Zにそろえる) / 中心を原点・足元をY=0・最大の辺を--sizeにそろえる。
# 出力は、glTFと同じ右手系・Y上(KenneyのOBJと同じ)。エンジン側への変換(左手系)は、ローダーが行う(ModelData::ConvertFromRightHanded)。
#
# ライセンス: CC BY など、作者の表示が必要な素材は、--title/--author/--source/--license を渡すと、CREDITS.txt に、
# 出典・ライセンス・「加工した(変換・切り出し・テクスチャの縮小)」旨を書く。THIRD_PARTY_LICENSES.md にも、別に追記すること。

import argparse
import math
import os
import sys

import bpy
import numpy as np


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="src", required=True, help="入力のGLB/glTFファイル")
    p.add_argument("--out-dir", required=True, help="出力フォルダ(無ければ作る)")
    p.add_argument("--name", required=True, help="出力のファイル名(拡張子なし)")
    p.add_argument("--rotate-y", type=float, default=0.0, help="Y軸まわりの回転(度)。glTFの座標で、正面を+Zにそろえるのに使う")
    p.add_argument("--crop", type=float, nargs=4, metavar=("X0", "X1", "Z0", "Z1"), help="残す範囲(glTFの座標。回転の前)")
    p.add_argument("--drop-ground", type=float, default=None, metavar="HEIGHT",
                   help="一番低い所からHEIGHT以内にある、上向きの面(地面・床)を削除する(単位はglTFの座標)")
    p.add_argument("--double-sided", action="store_true",
                   help="全ての三角形に、裏向きの複製(法線も反転)を足して、両面描画にする。壁が内向きに作られたモデル用(三角形の数は2倍になる)")
    p.add_argument("--tile", type=int, default=128, help="アトラスの1マスの大きさ(ピクセル)")
    p.add_argument("--size", type=float, default=1.0, help="水平方向の最大の辺の長さ(この大きさにそろえる)")
    p.add_argument("--title", default="")
    p.add_argument("--author", default="")
    p.add_argument("--source", default="")
    p.add_argument("--license", default="")
    return p.parse_args(argv)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


# ---------------------------------------------------------------------------
# 読み込み: 全てのメッシュを、三角形の列にする(glTFの座標。変形を焼き込み済み)
# ---------------------------------------------------------------------------
def import_model(path):
    """拡張子で、Blenderの読み込みを切り替える(.glb/.gltf、.fbx、.obj)。どれも、Blenderの座標(Z上)に直して取り込まれる。
    .objは、材質(.mtl)が無いと、色が失われる。FBX・GLBは、材質が中に入っているので、あれば、そちらを元にする。"""
    ext = os.path.splitext(path)[1].lower()
    if ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)
    elif ext == ".obj":
        bpy.ops.wm.obj_import(filepath=path)
    else:
        raise RuntimeError(f"未対応の形式です: {ext}")


def blender_to_gltf(v):
    """Blender(Z上)の座標(N,3)を、glTF(Y上)に直す: (x, y, z) -> (x, z, -y)。"""
    return np.stack([v[:, 0], v[:, 2], -v[:, 1]], axis=1)


def collect_triangles():
    """戻り値: tris(T,3,8)=頂点ごとの[x,y,z,nx,ny,nz,u,v]、mat_ids(T,)、materials(材質のリスト)。"""
    materials = []
    mat_index = {}
    all_tris = []
    all_mats = []

    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        tri_count = len(mesh.loop_triangles)
        if tri_count == 0:
            continue

        nv = len(mesh.vertices)
        co = np.empty(nv * 3, dtype=np.float32)
        mesh.vertices.foreach_get('co', co)
        co = co.reshape(nv, 3)

        nl = len(mesh.loops)
        loop_vert = np.empty(nl, dtype=np.int32)
        mesh.loops.foreach_get('vertex_index', loop_vert)

        normals = np.empty(nl * 3, dtype=np.float32)
        mesh.corner_normals.foreach_get('vector', normals)
        normals = normals.reshape(nl, 3)

        uv_layer = mesh.uv_layers.active
        if uv_layer is not None:
            uvs = np.empty(nl * 2, dtype=np.float32)
            uv_layer.data.foreach_get('uv', uvs)
            uvs = uvs.reshape(nl, 2)
        else:
            uvs = np.zeros((nl, 2), dtype=np.float32)

        tri_loops = np.empty(tri_count * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get('loops', tri_loops)
        tri_loops = tri_loops.reshape(tri_count, 3)
        tri_mat = np.empty(tri_count, dtype=np.int32)
        mesh.loop_triangles.foreach_get('material_index', tri_mat)

        # オブジェクトの変形(行列)を、位置・法線に焼き込む。
        m = np.array(obj.matrix_world, dtype=np.float64)
        rot = m[:3, :3]
        pos_w = (co.astype(np.float64) @ rot.T) + m[:3, 3]
        nrm_rot = np.linalg.inv(rot).T          # 法線は、逆転置行列で変換する(拡大・鏡像に対応)
        nrm_w = normals.astype(np.float64) @ nrm_rot.T
        length = np.linalg.norm(nrm_w, axis=1, keepdims=True)
        nrm_w = nrm_w / np.where(length < 1e-12, 1.0, length)

        pos_g = blender_to_gltf(pos_w)
        nrm_g = blender_to_gltf(nrm_w)

        vp = pos_g[loop_vert[tri_loops]]         # (T,3,3)
        vn = nrm_g[tri_loops]                    # (T,3,3)
        vu = uvs[tri_loops]                      # (T,3,2)
        tris = np.concatenate([vp, vn, vu], axis=2).astype(np.float32)

        # 鏡像(行列式が負)の時は、三角形の表裏が逆になるので、2番目と3番目の頂点を入れ替えて、表を保つ。
        if np.linalg.det(rot) < 0:
            tris = tris[:, [0, 2, 1], :]

        # 材質: オブジェクトごとのスロット -> 全体の通し番号
        slot_to_global = []
        for slot_mat in mesh.materials:
            key = slot_mat.name if slot_mat else "(none)"
            if key not in mat_index:
                mat_index[key] = len(materials)
                materials.append(slot_mat)
            slot_to_global.append(mat_index[key])
        if not slot_to_global:
            if "(none)" not in mat_index:
                mat_index["(none)"] = len(materials)
                materials.append(None)
            slot_to_global = [mat_index["(none)"]]
        slot_to_global = np.array(slot_to_global, dtype=np.int32)
        tri_mat = np.clip(tri_mat, 0, len(slot_to_global) - 1)

        all_tris.append(tris)
        all_mats.append(slot_to_global[tri_mat])

    return np.concatenate(all_tris, axis=0), np.concatenate(all_mats, axis=0), materials


def fix_winding(tris):
    """三角形の巻き順(表裏)を、頂点の法線に合わせる。戻り値: (直した配列, 直した数)。
    glTFの材質は「両面描画」にできるので、元のモデルには、巻き順が法線と逆の面が混ざっていることがある(Blenderや多くのビューアは、裏面も描くので気づかない)。
    このゲームは裏面を描かない(カリング)ので、そのままだと、壁などが消えて見える。
    幾何的な法線(巻き順から求めた向き)と、頂点の法線の平均が逆向きの三角形は、2番目と3番目の頂点を入れ替えて、表を法線の向きにそろえる。"""
    p0, p1, p2 = tris[:, 0, :3], tris[:, 1, :3], tris[:, 2, :3]
    geometric = np.cross(p1 - p0, p2 - p0)
    vertex_normal = tris[:, :, 3:6].sum(axis=1)
    flip = (geometric * vertex_normal).sum(axis=1) < 0.0
    fixed = tris.copy()
    fixed[flip] = tris[flip][:, [0, 2, 1], :]
    return fixed, int(flip.sum())


# ---------------------------------------------------------------------------
# 範囲の切り出し・地面の削除
# ---------------------------------------------------------------------------
def clip_polygon(poly, axis, value, keep_greater):
    out = []
    n = len(poly)
    for i in range(n):
        a = poly[i]
        b = poly[(i + 1) % n]
        sign = 1.0 if keep_greater else -1.0
        da = (a[axis] - value) * sign
        db = (b[axis] - value) * sign
        in_a = da >= 0.0
        in_b = db >= 0.0
        if in_a:
            out.append(a)
        if in_a != in_b:
            t = da / (da - db)
            out.append(a + (b - a) * t)
    return out


def crop_triangles(tris, mats, x0, x1, z0, z1):
    """x0<=x<=x1, z0<=z<=z1 の範囲を残す。範囲をまたぐ三角形は、切って、新しい頂点(法線・UVも補間)を作る。"""
    lo = tris[:, :, [0, 2]].min(axis=1)     # (T,2) x,z
    hi = tris[:, :, [0, 2]].max(axis=1)
    inside = (lo[:, 0] >= x0) & (hi[:, 0] <= x1) & (lo[:, 1] >= z0) & (hi[:, 1] <= z1)
    outside = (hi[:, 0] < x0) | (lo[:, 0] > x1) | (hi[:, 1] < z0) | (lo[:, 1] > z1)
    crossing = ~(inside | outside)

    kept = [tris[inside]]
    kept_mats = [mats[inside]]
    new_tris = []
    new_mats = []
    for idx in np.nonzero(crossing)[0]:
        poly = [tris[idx, k].astype(np.float64) for k in range(3)]
        for axis, value, greater in ((0, x0, True), (0, x1, False), (2, z0, True), (2, z1, False)):
            if not poly:
                break
            poly = clip_polygon(poly, axis, value, greater)
        for k in range(1, len(poly) - 1):
            new_tris.append(np.stack([poly[0], poly[k], poly[k + 1]]).astype(np.float32))
            new_mats.append(mats[idx])
    if new_tris:
        kept.append(np.stack(new_tris))
        kept_mats.append(np.array(new_mats, dtype=np.int32))
    return np.concatenate(kept, axis=0), np.concatenate(kept_mats, axis=0)


def drop_ground(tris, mats, height):
    """一番低い所からheight以内にある、水平(法線のYの絶対値>0.9。上向き・下向きの両方)の三角形(地面・床)を消す。
    地面は、巻き順が逆(下向き)で作られていることもあるので、向きは問わない。"""
    ymin = tris[:, :, 1].min()
    top = tris[:, :, 1].max(axis=1)
    p0, p1, p2 = tris[:, 0, :3], tris[:, 1, :3], tris[:, 2, :3]
    face_n = np.cross(p1 - p0, p2 - p0)
    length = np.linalg.norm(face_n, axis=1)
    flat = np.abs(face_n[:, 1]) > 0.9 * np.maximum(length, 1e-12)
    ground = flat & (top <= ymin + height)
    return tris[~ground], mats[~ground], int(ground.sum())


# ---------------------------------------------------------------------------
# アトラス
# ---------------------------------------------------------------------------
def find_base_color(mat):
    """材質の(リニアの基本色(r,g,b)、画像(無ければNone))。"""
    if mat is None or not mat.use_nodes:
        return (0.8, 0.8, 0.8), None
    bsdf = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        return (0.8, 0.8, 0.8), None
    sock = bsdf.inputs['Base Color']
    color = tuple(sock.default_value[:3])
    image = None
    if sock.is_linked:
        node = sock.links[0].from_node
        if node.type == 'TEX_IMAGE' and node.image is not None:
            image = node.image
    return color, image


def read_image_raw(image, size):
    """画像を、size x size に縮め(Blenderの機能)、保存されている値(sRGBのバイト/255)のまま、(size,size,4)で返す。行は下から上。"""
    image.colorspace_settings.name = 'Non-Color'     # 色の変換をさせない(保存されている値を、そのまま読む)
    image.scale(size, size)
    buf = np.empty(size * size * 4, dtype=np.float32)
    image.pixels.foreach_get(buf)
    return buf.reshape(size, size, 4)


def build_atlas(materials, used_ids, tris_by_mat_uv_range, tile, pad=2):
    """全ての材質を、1枚のアトラスにまとめる。戻り値: (atlas(H,W,4), W, H, 材質ごとの情報)。"""
    n = len(used_ids)
    cols = int(math.ceil(math.sqrt(n)))
    rows = int(math.ceil(n / cols))
    width, height = cols * tile, rows * tile
    atlas = np.ones((height, width, 4), dtype=np.float32)
    info = {}
    for k, mid in enumerate(used_ids):
        cx, cy = k % cols, k // cols
        x0, y0 = cx * tile, cy * tile
        color_lin, image = find_base_color(materials[mid])
        umin, umax, vmin, vmax = tris_by_mat_uv_range[mid]
        in_range = (umin >= -0.02 and umax <= 1.02 and vmin >= -0.02 and vmax <= 1.02)
        mode = "solid"
        if image is not None and in_range:
            inner = tile - 2 * pad
            raw = read_image_raw(image, inner)
            tile_pixels = np.pad(raw, ((pad, pad), (pad, pad), (0, 0)), mode='edge')   # 端を伸ばして、隣のマスの色が混ざらないようにする
            tile_pixels[:, :, 3] = 1.0
            mode = "texture"
        elif image is not None:
            raw = read_image_raw(image, 16)
            mean = raw[:, :, :3].reshape(-1, 3).mean(axis=0)                           # 繰り返しの模様は、平均の色(保存されている値のまま)
            tile_pixels = np.empty((tile, tile, 4), dtype=np.float32)
            tile_pixels[:, :] = (mean[0], mean[1], mean[2], 1.0)
            mode = "average(tiling)"
        else:
            rgb = linear_to_srgb(np.array(color_lin, dtype=np.float32))
            tile_pixels = np.empty((tile, tile, 4), dtype=np.float32)
            tile_pixels[:, :] = (rgb[0], rgb[1], rgb[2], 1.0)
        atlas[y0:y0 + tile, x0:x0 + tile] = tile_pixels
        info[mid] = {"mode": mode, "x0": x0, "y0": y0}
    return atlas, width, height, info


def remap_uvs(tris, mats, info, tile, width, height, pad=2):
    """アトラス上のUVに付け替える(新しい配列を返す)。"""
    out = tris.copy()
    for mid, i in info.items():
        sel = mats == mid
        if not sel.any():
            continue
        u = tris[sel, :, 6]
        v = tris[sel, :, 7]
        if i["mode"] == "texture":
            inner = tile - 2 * pad
            out[sel, :, 6] = (i["x0"] + pad + np.clip(u, 0.0, 1.0) * inner) / width
            out[sel, :, 7] = (i["y0"] + pad + np.clip(v, 0.0, 1.0) * inner) / height
        else:
            out[sel, :, 6] = (i["x0"] + tile * 0.5) / width
            out[sel, :, 7] = (i["y0"] + tile * 0.5) / height
    return out


# ---------------------------------------------------------------------------
# 出力
# ---------------------------------------------------------------------------
def write_obj(path, name, tris):
    """頂点(位置・UV・法線)の重複をまとめて、OBJにする。"""
    flat = tris.reshape(-1, 8)
    key = np.round(flat, 5)
    uniq, inverse = np.unique(key, axis=0, return_inverse=True)
    inverse = inverse.reshape(-1)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Converted from glTF by tools/convert_model.py (geometry baked, materials merged into one atlas texture)\n")
        f.write(f"mtllib {name}.mtl\n")
        f.write(f"o {name}\n")
        for r in uniq:
            f.write(f"v {r[0]:.5f} {r[1]:.5f} {r[2]:.5f}\n")
        for r in uniq:
            f.write(f"vt {r[6]:.5f} {r[7]:.5f}\n")
        for r in uniq:
            f.write(f"vn {r[3]:.5f} {r[4]:.5f} {r[5]:.5f}\n")
        f.write("usemtl atlas\n")
        f.write("s 1\n")
        idx = inverse.reshape(-1, 3) + 1
        for a, b, c in idx:
            f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
    return len(uniq)


def write_mtl(path, name):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Atlas material for the converted model\n")   # 「Blender」という語は書かない(ローダーが、Kdをリニアとして扱ってしまうため)
        f.write("newmtl atlas\n")
        f.write("Kd 1.000 1.000 1.000\n")
        f.write(f"map_Kd {name}_atlas.png\n")


def save_atlas(path, atlas, width, height):
    img = bpy.data.images.new("atlas", width, height, alpha=False)
    img.colorspace_settings.name = 'Non-Color'      # 値を、そのままバイトにする(sRGBの値を、既に入れてあるため)
    img.pixels.foreach_set(atlas.reshape(-1))
    img.update()
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()


def main():
    args = parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    import_model(args.src)

    tris, mats, materials = collect_triangles()
    print(f"[convert] loaded: {len(tris)} triangles, {len(materials)} materials")
    tris, flipped = fix_winding(tris)
    print(f"[convert] fixed winding of {flipped} triangles (back faces would be culled by the game)")

    if args.crop:
        x0, x1, z0, z1 = args.crop
        tris, mats = crop_triangles(tris, mats, x0, x1, z0, z1)
        print(f"[convert] cropped to x[{x0},{x1}] z[{z0},{z1}]: {len(tris)} triangles")
    if args.drop_ground is not None:
        tris, mats, dropped = drop_ground(tris, mats, args.drop_ground)
        print(f"[convert] dropped {dropped} ground triangles: {len(tris)} left")
    if len(tris) == 0:
        print("[convert] ERROR: no triangles left")
        sys.exit(1)
    if args.double_sided:
        # glTFの「両面描画」と同じ見え方にする: 裏向きの三角形(2番目と3番目の頂点を入れ替え、法線を反転)を足す。
        back = tris[:, [0, 2, 1], :].copy()
        back[:, :, 3:6] *= -1.0
        tris = np.concatenate([tris, back], axis=0)
        mats = np.concatenate([mats, mats], axis=0)
        print(f"[convert] double-sided: {len(tris)} triangles")

    # 使う材質だけで、アトラスを作る。
    used_ids = sorted(set(int(m) for m in np.unique(mats)))
    uv_ranges = {}
    for mid in used_ids:
        uv = tris[mats == mid][:, :, 6:8].reshape(-1, 2)
        uv_ranges[mid] = (float(uv[:, 0].min()), float(uv[:, 0].max()), float(uv[:, 1].min()), float(uv[:, 1].max()))
    atlas, width, height, info = build_atlas(materials, used_ids, uv_ranges, args.tile)
    tris = remap_uvs(tris, mats, info, args.tile, width, height)
    modes = {}
    for i in info.values():
        modes[i["mode"]] = modes.get(i["mode"], 0) + 1
    print(f"[convert] atlas {width}x{height}, materials used {len(used_ids)}: {modes}")

    # 回転 -> 中心・足元・大きさをそろえる。
    theta = math.radians(args.rotate_y)
    c, s = math.cos(theta), math.sin(theta)
    for off in (0, 3):                       # 位置と法線
        x = tris[:, :, off].copy()
        z = tris[:, :, off + 2].copy()
        tris[:, :, off] = x * c + z * s
        tris[:, :, off + 2] = -x * s + z * c
    pos = tris[:, :, :3].reshape(-1, 3)
    mn, mx = pos.min(axis=0), pos.max(axis=0)
    center_x, center_z = (mn[0] + mx[0]) * 0.5, (mn[2] + mx[2]) * 0.5
    scale = args.size / max(mx[0] - mn[0], mx[2] - mn[2])
    tris[:, :, 0] = (tris[:, :, 0] - center_x) * scale
    tris[:, :, 1] = (tris[:, :, 1] - mn[1]) * scale
    tris[:, :, 2] = (tris[:, :, 2] - center_z) * scale
    length = np.linalg.norm(tris[:, :, 3:6], axis=2, keepdims=True)
    tris[:, :, 3:6] = tris[:, :, 3:6] / np.where(length < 1e-12, 1.0, length)

    obj_path = os.path.join(args.out_dir, f"{args.name}.obj")
    mtl_path = os.path.join(args.out_dir, f"{args.name}.mtl")
    png_path = os.path.join(args.out_dir, f"{args.name}_atlas.png")
    vertex_count = write_obj(obj_path, args.name, tris)
    write_mtl(mtl_path, args.name)
    save_atlas(png_path, atlas, width, height)

    size = (tris[:, :, :3].reshape(-1, 3).max(axis=0) - tris[:, :, :3].reshape(-1, 3).min(axis=0))
    print(f"[convert] wrote {obj_path}: {len(tris)} triangles, {vertex_count} unique vertices, size x={size[0]:.3f} y={size[1]:.3f} z={size[2]:.3f}")

    if args.title or args.author:
        with open(os.path.join(args.out_dir, "CREDITS.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(f"{args.title}\n")
            f.write(f"Author : {args.author}\n")
            f.write(f"Source : {args.source}\n")
            f.write(f"License: {args.license}\n")
            source_format = os.path.splitext(args.src)[1].lstrip(".").upper()
            f.write(f"\nModified: converted from {source_format} to OBJ, geometry transformed and merged, materials merged into one texture atlas\n")
            f.write("(textures reduced in size; repeating textures replaced by an average colour")
            if args.crop:
                f.write("; part of the original scene cut out")
            if args.drop_ground is not None:
                f.write("; ground removed")
            f.write(").\n")
            f.write("Conversion script: tools/convert_model.py\n")


main()

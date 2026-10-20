#include "FbxModelLoader.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>
#include <vector>
#include "ufbx.h"
#include "Debug.h"

namespace fs = std::filesystem;

namespace
{
    // ufbxのシーンを確実に解放するための入れ物。
    struct SceneDeleter
    {
        void operator()(ufbx_scene* scene) const { ufbx_free_scene(scene); }
    };
    using ScenePtr = std::unique_ptr<ufbx_scene, SceneDeleter>;

    std::string ToLower(std::string text)
    {
        for (char& c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    bool IsImageExtension(const std::string& lowerExtension)
    {
        return lowerExtension == ".png" || lowerExtension == ".jpg" || lowerExtension == ".jpeg" ||
            lowerExtension == ".tga" || lowerExtension == ".bmp" || lowerExtension == ".dds";
    }

    bool FileExists(const fs::path& path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    // nameと同じ名前(拡張子を除く)の画像を、baseDirectoryから親へ最大4階層さかのぼりながら探す。
    // 各階層で、そのフォルダ直下 → 「Textures」フォルダ直下 → 「Textures」の下のサブフォルダ(Vehicleなど)の順に見る。
    // 見つかればそのパス、無ければ空を返す。
    std::string FindTextureByName(const fs::path& baseDirectory, const std::string& name)
    {
        if (name.empty())
        {
            return std::string();
        }

        static const char* kExtensions[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds" };
        const std::string lowerName = ToLower(name);

        fs::path directory = baseDirectory;
        for (int level = 0; level < 4; ++level)
        {
            for (const char* extension : kExtensions)
            {
                if (FileExists(directory / (name + extension)))
                {
                    return (directory / (name + extension)).generic_string();
                }
                if (FileExists(directory / "Textures" / (name + extension)))
                {
                    return (directory / "Textures" / (name + extension)).generic_string();
                }
            }

            // サブフォルダ(Textures/Vehicle/Taxi.png のような置き方)を、大文字小文字を区別せずに探す。
            std::error_code ec;
            fs::path texturesRoot = directory / "Textures";
            if (fs::is_directory(texturesRoot, ec))
            {
                for (fs::recursive_directory_iterator it(texturesRoot, ec), end; !ec && it != end; it.increment(ec))
                {
                    std::error_code fileEc;
                    if (!it->is_regular_file(fileEc))
                    {
                        continue;
                    }
                    const fs::path& candidate = it->path();
                    if (ToLower(candidate.stem().string()) == lowerName && IsImageExtension(ToLower(candidate.extension().string())))
                    {
                        return candidate.generic_string();
                    }
                }
            }

            fs::path parent = directory.parent_path();
            if (parent.empty() || parent == directory)
            {
                break;
            }
            directory = parent;
        }

        return std::string();
    }

    // マテリアルが使うテクスチャのファイルパスを求める。見つからなければ空。
    // ①FBXが参照しているファイル(相対 → 同じ名前でmodelDirectory直下 → 同じ名前の画像を探索)、
    // ②マテリアル名と同じ名前の画像、③modelStem(モデルのファイル名。分かる場合だけ)と同じ名前の画像。
    std::string ResolveMaterialTexture(const ufbx_material& material, const fs::path& modelDirectory, const std::string& modelStem)
    {
        const ufbx_texture* texture = material.pbr.base_color.texture;
        if (texture)
        {
            if (texture->content.size > 0)
            {
                Debug::Warning("FbxModelLoader: 埋め込みテクスチャは未対応です(マテリアル: " + std::string(material.name.data) + ")");
            }

            const std::string relative = texture->relative_filename.data ? texture->relative_filename.data : "";
            const std::string filename = texture->filename.data ? texture->filename.data : "";

            if (!relative.empty() && FileExists(modelDirectory / relative))
            {
                return (modelDirectory / relative).generic_string();
            }
            if (!filename.empty())
            {
                fs::path baseName = fs::path(filename).filename();
                if (FileExists(modelDirectory / baseName))
                {
                    return (modelDirectory / baseName).generic_string();
                }
                std::string found = FindTextureByName(modelDirectory, fs::path(filename).stem().string());
                if (!found.empty())
                {
                    return found;
                }
            }
        }

        const std::string materialName = material.name.data ? material.name.data : "";
        std::string found = FindTextureByName(modelDirectory, materialName);
        if (found.empty())
        {
            found = FindTextureByName(modelDirectory, modelStem);
        }
        // 色違いが「名前_1、名前_2…」と番号付きで用意されているパック(SimplePoly Cityの乗り物など)では、
        // 番号なしの名前の画像は無い。その場合は最初の色違い(_1)を使う。
        if (found.empty() && !modelStem.empty())
        {
            found = FindTextureByName(modelDirectory, modelStem + "_1");
        }
        if (found.empty() && !materialName.empty())
        {
            found = FindTextureByName(modelDirectory, materialName + "_1");
        }
        return found;
    }

    ufbx_vec3 Normalized(ufbx_vec3 v)
    {
        double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length < 1e-12)
        {
            return ufbx_vec3{ 0.0, 1.0, 0.0 };
        }
        return ufbx_vec3{ v.x / length, v.y / length, v.z / length };
    }

    // 1つのマテリアルを、頂点の色・テクスチャの有無として使うための情報。
    struct MaterialInfo
    {
        bool hasTexture = false;
        float r = 1.0f, g = 1.0f, b = 1.0f;
    };

    // メモリ上のFBXを読んでoutに入れる(ファイルから読む場合もメモリから読む場合も共通)。
    // modelStemはモデルのファイル名(拡張子なし)。メモリから読む場合は分からないので空でよい。
    bool LoadImpl(const void* data, size_t size, const std::string& baseDirectory, const std::string& modelStem, ModelData& out)
    {
        out.Clear();
        if (!data || size == 0)
        {
            Debug::Error("FbxModelLoader failed : empty data");
            return false;
        }

        ufbx_load_opts opts = {};
        opts.target_axes = ufbx_axes_right_handed_y_up; // OBJと同じ右手系・Y上
        opts.target_unit_meters = 1.0f;                 // メートルに換算(cm単位で作られたFBXなども揃う)
        opts.generate_missing_normals = true;           // 法線が無いモデルは面の向きから作る
        ufbx_error error;
        ScenePtr scene(ufbx_load_memory(data, size, &opts, &error));
        if (!scene)
        {
            Debug::Error(std::string("FbxModelLoader: ufbx load failed : ") + error.description.data);
            return false;
        }

        const fs::path modelDirectory(baseDirectory);

        float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
        float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
        bool textureWarned = false;

        // メッシュを持つノードごとに、ワールド変換を頂点に焼き込んで1つの頂点リストにまとめる
        // (静的モデルとして描くので、ノードの階層・移動・回転・拡縮はここで消す)。
        for (size_t nodeIndex = 0; nodeIndex < scene->nodes.count; ++nodeIndex)
        {
            const ufbx_node* node = scene->nodes.data[nodeIndex];
            const ufbx_mesh* mesh = node->mesh;
            if (!mesh || mesh->num_triangles == 0)
            {
                continue;
            }

            const ufbx_matrix toWorld = node->geometry_to_world;
            const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&toWorld);
            // 鏡像になる変換(行列式が負)では、三角形の向きが逆になるので、頂点の順を入れ替えて直す。
            const bool flipWinding = ufbx_matrix_determinant(&toWorld) < 0.0;

            std::vector<MaterialInfo> materialInfos(mesh->materials.count);
            for (size_t m = 0; m < mesh->materials.count; ++m)
            {
                const ufbx_material* material = mesh->materials.data[m];
                if (!material)
                {
                    continue;
                }

                std::string texture = ResolveMaterialTexture(*material, modelDirectory, modelStem);
                MaterialInfo& info = materialInfos[m];
                if (!texture.empty())
                {
                    if (out.texturePath.empty())
                    {
                        out.texturePath = texture;
                    }
                    else if (out.texturePath != texture && !textureWarned)
                    {
                        Debug::Warning("FbxModelLoader: 複数のテクスチャを使うモデルです。最初の1枚だけを使います : " + out.texturePath);
                        textureWarned = true;
                    }
                    // テクスチャがある面は、OBJのKd=1と同じく白(テクスチャの色をそのまま出す)。
                    info.hasTexture = true;
                }
                else
                {
                    // テクスチャが無い面は、マテリアルの基本色を頂点色にする(OBJのKdと同じ扱い)。
                    ufbx_vec3 color = material->pbr.base_color.value_vec3;
                    info.r = static_cast<float>(color.x);
                    info.g = static_cast<float>(color.y);
                    info.b = static_cast<float>(color.z);
                }
            }

            std::vector<uint32_t> triangleIndices(mesh->max_face_triangles * 3);
            out.vertices.reserve(out.vertices.size() + mesh->num_triangles * 3);

            for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex)
            {
                const ufbx_face face = mesh->faces.data[faceIndex];
                const uint32_t triangleCount = ufbx_triangulate_face(triangleIndices.data(), triangleIndices.size(), mesh, face);

                MaterialInfo color;
                if (faceIndex < mesh->face_material.count)
                {
                    uint32_t materialIndex = mesh->face_material.data[faceIndex];
                    if (materialIndex < materialInfos.size())
                    {
                        color = materialInfos[materialIndex];
                    }
                }
                const float vr = color.hasTexture ? 1.0f : color.r;
                const float vg = color.hasTexture ? 1.0f : color.g;
                const float vb = color.hasTexture ? 1.0f : color.b;

                for (uint32_t t = 0; t < triangleCount; ++t)
                {
                    uint32_t corners[3] = { triangleIndices[t * 3 + 0], triangleIndices[t * 3 + 1], triangleIndices[t * 3 + 2] };
                    if (flipWinding)
                    {
                        std::swap(corners[1], corners[2]);
                    }

                    for (uint32_t corner : corners)
                    {
                        ufbx_vec3 position = ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, corner));

                        ufbx_vec3 normal{ 0.0, 1.0, 0.0 };
                        if (mesh->vertex_normal.exists)
                        {
                            normal = Normalized(ufbx_transform_direction(&normalToWorld, ufbx_get_vertex_vec3(&mesh->vertex_normal, corner)));
                        }

                        ufbx_vec2 uv{ 0.0, 0.0 };
                        if (mesh->vertex_uv.exists)
                        {
                            uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
                        }

                        Vertex vertex;
                        vertex.x = static_cast<float>(position.x);
                        vertex.y = static_cast<float>(position.y);
                        vertex.z = static_cast<float>(position.z);
                        vertex.nx = static_cast<float>(normal.x);
                        vertex.ny = static_cast<float>(normal.y);
                        vertex.nz = static_cast<float>(normal.z);
                        vertex.u = static_cast<float>(uv.x);
                        vertex.v = 1.0f - static_cast<float>(uv.y); // UVの上下は、OBJローダーと同じくDirectX用にVを反転する
                        vertex.r = vr;
                        vertex.g = vg;
                        vertex.b = vb;
                        vertex.a = 1.0f;
                        out.vertices.push_back(vertex);

                        minX = (std::min)(minX, vertex.x); maxX = (std::max)(maxX, vertex.x);
                        minY = (std::min)(minY, vertex.y); maxY = (std::max)(maxY, vertex.y);
                        minZ = (std::min)(minZ, vertex.z); maxZ = (std::max)(maxZ, vertex.z);
                    }
                }
            }
        }

        if (out.vertices.empty())
        {
            Debug::Warning("FbxModelLoader: 三角形がありませんでした(メッシュの無いFBXです)");
            return false;
        }

        out.boundsMin = { minX, minY, minZ };
        out.boundsMax = { maxX, maxY, maxZ };

        // ここまでは右手系・Y上(ufbxの変換結果)。エンジンの左手系に変換して返す(鏡像・文字の鏡文字を防ぐ)。
        out.ConvertFromRightHanded();
        return true;
    }
}

bool FbxModelLoader::LoadFromFile(const std::string& filePath, ModelData& out)
{
    // ファイルを自分で読み、メモリ版と同じ処理に渡す。ufbx_load_fileはUTF-8のパスを期待するが、
    // このプロジェクトのパスはシステムの文字コード(ANSI)の文字列なので、ここで開けば食い違わない。
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        Debug::Error("FbxModelLoader::LoadFromFile failed : cannot open fbx file : " + filePath);
        return false;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const fs::path path(filePath);
    return LoadImpl(bytes.data(), bytes.size(), path.parent_path().generic_string(), path.stem().string(), out);
}

bool FbxModelLoader::LoadFromMemory(const void* data, size_t size, const std::string& baseDirectory, ModelData& out)
{
    return LoadImpl(data, size, baseDirectory, std::string(), out);
}

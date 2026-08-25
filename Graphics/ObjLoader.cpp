#include "ObjLoader.h"
#include"Debug.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct Float3
    {
        float x, y, z;
    };
    struct Float2
    {
        float u, v;
    };

}

//int ObjLoader::ParseVertexIndex(const std::string& token)
//{
//    // "3/1/2" -> 3
//    // "3//2"  -> 3
//    // "3"     -> 3
//    std::stringstream ss(token);
//    std::string indexStr;
//    std::getline(ss, indexStr, '/');//仮なので頂点番号だけ読み込む
//
//    if (indexStr.empty())
//    {
//        return -1;
//    }
//
//    return std::stoi(indexStr);
//}

ObjIndex ObjLoader::ParseFaceToken(const std::string& token)
{
    // "3/5/2" → position=3, uv=5, normal=2
    // "3//2"  → position=3, uvなし, normal=2
    // "3/5"   → position=3, uv=5, normalなし
    // "3"     → positionのみ

    ObjIndex index{};
    index.positionIndex = -1;
    index.texcoordIndex = -1;
    index.normalIndex = -1;

    std::stringstream ss(token);
    std::string part;

    // 1つ目：頂点インデックス
    if (std::getline(ss, part, '/'))
    {
        if (!part.empty())
        {
            //文字列をintに変換する
            index.positionIndex = std::stoi(part);
        }
    }

    // 2つ目：UVインデックス
    if (std::getline(ss, part, '/'))
    {
        if (!part.empty())
        {
           
            index.texcoordIndex = std::stoi(part);
        }
    }

    // 3つ目：法線インデックス
    if (std::getline(ss, part, '/'))
    {
        if (!part.empty())
        {
            index.normalIndex = std::stoi(part);
        }
    }

    return index;
}

bool ObjLoader::Load(const std::string& filePath, std::vector<ObjVertex>& outVertices, std::string& outTexturePath)
{
    //一旦中身をクリア
    outVertices.clear();
    outTexturePath.clear();

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        Debug::Error("ObjLoader::Load failed : cannot open obj file : " + filePath);
        return false;
    }

    // OBJの各データを保持
    std::vector<Float3> positions;// 頂点座標を格納
    std::vector<Float2> texcoords;// UV座標を格納
    std::vector<Float3> normals;  // 法線情報を格納
    std::string line;             // 1行分の文字列を格納


    //========================================
    // ファイルを1行ずつ解析
    //========================================
    while (std::getline(file, line))
    {
        if (line.empty())  
        {
            continue;   
        }

        std::istringstream iss(line);//文字列を入力として扱う
        std::string type;
        iss >> type;


       //========================================
       // 頂点座標（v）
       //========================================
        if (type == "v")
        {
            Float3 pos{};
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
       //========================================
       // UV座標（vt）
       //========================================
        else if (type == "vt")
        {
            Float2 uv{};
            iss >> uv.u >> uv.v;

            // OBJ系のV座標をDirectX用に変換
            uv.v = 1.0f - uv.v;

            texcoords.push_back(uv);
        }
       //========================================
       // 法線（vn）
       //========================================
        else if (type == "vn")
        {
            Float3 normal{};
            iss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        //========================================
        // マテリアル
        //========================================
        else if (type == "mtllib")
        {
            std::string mtlFileName;



            iss >> mtlFileName;

            if (!mtlFileName.empty())
            {
                // いったん同じフォルダにある前提
                std::string directory;

                size_t slashPos = filePath.find_last_of("/\\");
                if (slashPos != std::string::npos)
                {
                    directory = filePath.substr(0, slashPos + 1);
                }

                std::string mtlPath = directory + mtlFileName;

                //マテリアルなしのOBJの可能性もあり
                if (!LoadMtl(mtlPath, directory, outTexturePath))
                {
                    Debug::Warning("ObjLoader::LoadMtl failed : " + mtlPath);
                }


                LoadMtl(mtlPath, directory, outTexturePath);
            }


        }
       //========================================
       // 面情報（f）
       //========================================
        else if (type == "f")//組み合わせ方
        {
            std::vector<std::string> faceTokens;//データ保存用
            std::string token;

            // "f 1/1/1 2/2/2 3/3/3" を分解
            while (iss >> token)
            {
                faceTokens.push_back(token);
            }

            // 最低3頂点必要、三角形未満は無視
            if (faceTokens.size() < 3)
            {
                continue;
            }

            //========================================
            // ポリゴンを三角形に分割（扇形分割）
            //========================================
            
            //三角形として描画したいため読み込んだobjを三角形に分割して読み込む
            // 三角形ならそのまま
            // 四角形以上なら扇形分割
            // 例: 0,1,2,3 -> (0,1,2), (0,2,3)
            for (size_t i = 1; i + 1 < faceTokens.size(); ++i)
            {
                ObjIndex idx0 = ParseFaceToken(faceTokens[0]);
                ObjIndex idx1 = ParseFaceToken(faceTokens[i]);
                ObjIndex idx2 = ParseFaceToken(faceTokens[i + 1]);

                // 頂点インデックスが不正ならスキップ
                if (idx0.positionIndex <= 0 || idx1.positionIndex <= 0 || idx2.positionIndex <= 0)
                {
                    continue;
                }

                // 範囲外アクセス防止
                if (idx0.positionIndex > (int)positions.size() ||
                    idx1.positionIndex > (int)positions.size() ||
                    idx2.positionIndex > (int)positions.size())
                {
                    continue;
                }

                // OBJは1始まり → C++は0始まり
                const Float3& p0 = positions[idx0.positionIndex - 1];
                const Float3& p1 = positions[idx1.positionIndex - 1];
                const Float3& p2 = positions[idx2.positionIndex - 1];

                // デフォルト値（存在しない場合）
                Float2 uv0{ 0.0f, 0.0f };
                Float2 uv1{ 0.0f, 0.0f };
                Float2 uv2{ 0.0f, 0.0f };

                Float3 n0{ 0.0f, 0.0f, 0.0f };
                Float3 n1{ 0.0f, 0.0f, 0.0f };
                Float3 n2{ 0.0f, 0.0f, 0.0f };

                //========================================
                // UV取得
                //========================================

                if (idx0.texcoordIndex > 0 && idx0.texcoordIndex <= (int)texcoords.size())
                {
                    uv0 = texcoords[idx0.texcoordIndex - 1];
                }
                if (idx1.texcoordIndex > 0 && idx1.texcoordIndex <= (int)texcoords.size())
                {
                    uv1 = texcoords[idx1.texcoordIndex - 1];
                }
                if (idx2.texcoordIndex > 0 && idx2.texcoordIndex <= (int)texcoords.size())
                {
                    uv2 = texcoords[idx2.texcoordIndex - 1];
                }

                //========================================
                // 法線取得
                //========================================

                if (idx0.normalIndex > 0 && idx0.normalIndex <= (int)normals.size())
                {
                    n0 = normals[idx0.normalIndex - 1];
                }
                if (idx1.normalIndex > 0 && idx1.normalIndex <= (int)normals.size())
                {
                    n1 = normals[idx1.normalIndex - 1];
                }
                if (idx2.normalIndex > 0 && idx2.normalIndex <= (int)normals.size())
                {
                    n2 = normals[idx2.normalIndex - 1];
                }

                //========================================
                // 頂点として登録（GPU用）
                //========================================

                outVertices.push_back({p0.x, p0.y, p0.z, n0.x, n0.y, n0.z,uv0.u, uv0.v,1.0f, 1.0f, 1.0f, 1.0f});

                outVertices.push_back({p1.x, p1.y, p1.z,n1.x, n1.y, n1.z,uv1.u, uv1.v,1.0f, 1.0f, 1.0f, 1.0f});

                outVertices.push_back({p2.x, p2.y, p2.z,n2.x, n2.y, n2.z,uv2.u, uv2.v,1.0f, 1.0f, 1.0f, 1.0f});


            }
        }
    }

    return !outVertices.empty();
}

// MTLファイルを読み込み、マテリアル情報を取得する
// 今は map_Kd（テクスチャパス）のみ取得する
bool ObjLoader::LoadMtl(
    const std::string& mtlPath,
    const std::string& directory,
    std::string& outTexturePath
)
{
    std::ifstream file(mtlPath);
    if (!file.is_open())
    {
        Debug::Warning("ObjLoader::LoadMtl failed : cannot open mtl file : " + mtlPath);
        return false;
    }

    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        std::istringstream iss(line);
        std::string type;
        iss >> type;


        // テクスチャ（拡散マップ）を取得
        if (type == "map_Kd")
        {
            std::string textureFileName;
            iss >> textureFileName;

            if (!textureFileName.empty())
            {
                outTexturePath = directory + textureFileName;
                Debug::Info("ObjLoader::LoadMtl map_Kd : " + outTexturePath);
                return true;
            }

            if (!textureFileName.empty())
            {
                // OBJと同じディレクトリを付けてフルパスにする
                outTexturePath = directory + textureFileName;
                return true;
            }
        }
    }

    return false;
}
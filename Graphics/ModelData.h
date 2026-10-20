#pragma once
#include <string>
#include <utility>
#include <vector>
#include <DirectXMath.h>
#include "Vertex.h"

// モデルファイル(OBJ/FBXなど)を読んだ結果。GPUに依存しない、CPU側のメッシュデータ。
// 形式ごとのローダー(IModelLoader)がこの形に揃えて返し、Modelがそれを頂点バッファ・テクスチャにする。
// 「どの形式から読んだか」はここから先に漏らさない(描画側は形式を知らなくてよい)。
struct ModelData
{
    // 三角形リスト(インデックス無し。3頂点で1三角形)。
    // 座標系は**エンジンの座標系**(左手系・Y上・+Zが前方)。描画(XMMatrixLookAtLH)と同じ系なので、
    // 鏡像にならず、看板の文字も正しく読める。三角形の表は時計回り(Direct3Dの既定のカリングに合う)。
    // OBJ/FBXなど右手系で作られたファイルを読むローダーは、返す前に ConvertFromRightHanded() で変換すること。
    std::vector<Vertex> vertices;

    // 拡散テクスチャのファイルパス(無ければ空)。現状はモデル全体で1枚だけ扱う。
    std::string texturePath;

    // 頂点から求めたローカル空間のバウンディングボックス(頂点が無ければ0)。
    DirectX::XMFLOAT3 boundsMin = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 boundsMax = { 0.0f, 0.0f, 0.0f };

    void Clear()
    {
        vertices.clear();
        texturePath.clear();
        boundsMin = { 0.0f, 0.0f, 0.0f };
        boundsMax = { 0.0f, 0.0f, 0.0f };
    }

    // 右手系・Y上(OBJ、FBX、glTF、Blenderなどの標準)で作られた頂点を、エンジンの左手系に変換する。
    // 右手系のデータを、そのまま左手系の画面で見ると鏡像になる(看板の文字が鏡文字になる)ため、読み込み時に1回だけ直す。
    //   ・Zを反転する(位置・法線)。右手系の「前方=-Z」が、エンジンの「前方=+Z」になる。
    //   ・鏡に映すと三角形の表裏が逆になるので、各三角形の2番目と3番目の頂点を入れ替えて、表を保つ。
    //   ・バウンディングボックスのZを反転(大小も入れ替える)。
    // 頂点数に比例する単純な処理で、描画の毎フレームの負担は増えない(読み込み時に1回だけ)。
    // 同じモデルに2回呼ぶと元に戻ってしまうので、ローダーが1回だけ呼ぶ。
    void ConvertFromRightHanded()
    {
        for (Vertex& v : vertices)
        {
            v.z = -v.z;
            v.nz = -v.nz;
        }
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            std::swap(vertices[i + 1], vertices[i + 2]);
        }
        float newMinZ = -boundsMax.z;
        float newMaxZ = -boundsMin.z;
        boundsMin.z = newMinZ;
        boundsMax.z = newMaxZ;
    }
};

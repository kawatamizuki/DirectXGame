#pragma once
#include "IModelLoader.h"

// FBX形式のローダー(外部ライブラリufbxを使う)。今は静的なモデル(建物・小物など)だけを読む。
// スケルトン・スキン・アニメーションは読まない(キャラクターを使う段階で追加する)。
//
// 座標系: まずufbxで右手系・Y上・メートルに揃え(ufbx_axes_right_handed_y_up)、最後にエンジンの左手系へ
// 変換して返す(ModelData::ConvertFromRightHanded)。UVの上下(Vを反転)は、既存のOBJローダーと同じ約束。
// 注意: モデルの正面の向きは、ファイルが宣言している軸に従う。パックによっては実際の正面と食い違うことがある
// (SimplePoly Cityの建物は+Z側が正面)。向きの補正は、そのモデルを使う側(建物の定義など)で行う。
//
// テクスチャ: FBXが参照しているファイルを探し、無ければ「マテリアル名(次にファイル名)と同じ名前の画像」を
// モデルの近くの「Textures」フォルダ(親フォルダへ数階層さかのぼる)から探す。
// (SimplePoly CityのFBXはテクスチャの参照を持たず、Textures/<マテリアル名>.png が対応する約束のため。)
// 現状はモデル全体で1枚のテクスチャだけを扱う(最初に見つかった1枚)。
class FbxModelLoader : public IModelLoader
{
public:
    const char* Name() const override { return "FBX (ufbx)"; }
    bool SupportsExtension(const std::string& extension) const override { return extension == ".fbx"; }
    bool LoadFromFile(const std::string& filePath, ModelData& out) override;

    // メモリから読む場合は、ファイル名が分からないので、テクスチャはFBXの参照とマテリアル名だけで探す。
    bool LoadFromMemory(const void* data, size_t size, const std::string& baseDirectory, ModelData& out) override;
};

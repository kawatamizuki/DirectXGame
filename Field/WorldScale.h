#pragma once

// ゲームの世界の「大きさ」の基準を、1か所に集める。
// ここには、2つの別々の大きさがある。混ぜないこと。
//  ・長さの基準(kLengthUnit): 建物・道幅・住民・移動の速さ・カメラなどの「見た目の大きさ」の基準。最初のマス1.0の頃の1マス分。
//  ・マス(格子)の細かさ(kCellSize): 建物や道路を置く時の、置ける位置の細かさ。1マスを何分割するかを kSubdivision で決める。
// 分割しても、世界の広さ・建物の大きさ・道幅は変わらない(置ける位置が細かくなるだけ)。
// 建物・飾りの大きさは、長さの基準1つ分に収まるよう自動で決まる(BuildController/PropPlacer)。
// カメラの一番引ける距離だけは、フィールドの広さに合わせる(下のkCamera...)。
namespace WorldScale
{
    // 長さの基準(ワールド単位)。古いセーブデータは、この大きさで保存されている(セーブの"lengthUnit"。記録が無い古いセーブは、当時のマスの大きさ)。
    constexpr float kLengthUnit = 1.0f;

    // 最初のマスの大きさ。古いセーブデータにマスの大きさの記録が無い時の値(記録を足す前は、全てこの大きさだった)。
    constexpr float kLegacyCellSize = 1.0f;

    // 最初の1マスを、縦横それぞれ何分割したか(2なら、1マスが4つに分かれる)。
    constexpr int kSubdivision = 2;

    // 1マスの一辺(ワールド単位)。建物・道路を置く格子の細かさ。
    constexpr float kCellSize = kLegacyCellSize / static_cast<float>(kSubdivision);

    // フィールドのマスの数。世界の広さ(最初は20x20)は変えないので、分割した分だけ増える。
    constexpr int kFieldWidthCells = 20 * kSubdivision;
    constexpr int kFieldDepthCells = 20 * kSubdivision;

    // 最初のマス1.0の頃に決めた長さ(道幅・速さ・距離のしきい値など)を、今の長さの基準に合わせる。
    // 「最初の1マスの何倍」で決めた値は、この関数を通せば、長さの基準が変わっても同じ比率を保てる。
    // (マスの細かさ kCellSize には連動しない。マスを細かくしても、道幅や住民の大きさは変わらない)
    constexpr float FromLegacy(float legacyLength)
    {
        return legacyLength * (kLengthUnit / kLegacyCellSize);
    }

    // 道路の建設費の「1マス」の長さ。マスを細かくしても、長さあたりの値段が変わらないよう、最初の1マス分で数える。
    constexpr float kRoadPriceCellLength = kLengthUnit;

    // 画面の遠さ(カメラの描画の奥行き。ニア/ファー)。フィールドが大きくなっても、端が欠けないようにする。
    constexpr float kCameraNearPlane = 0.1f;
    constexpr float kCameraFarPlane = 400.0f;

    // 街づくりのカメラの距離(注視点からカメラまで)。
    // 最初のズームと一番寄る距離は、ワールド単位の「絶対」の値(最初のマス1.0の時のカメラと同じ)。
    constexpr float kCameraDefaultDistance = 14.0f;
    constexpr float kCameraMinDistance = 4.0f;
    // 一番引ける距離だけは、フィールドの幅の何倍か。世界が大きくなっても、一番引けば全体が見えるようにする(幅20の時の40と同じ比率)。
    constexpr float kCameraMaxDistanceFieldRatio = 2.0f;
}

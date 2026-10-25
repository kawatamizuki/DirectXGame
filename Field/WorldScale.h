#pragma once

// ゲームの世界の「大きさ」の基準を、1か所に集める。
// 1マスの大きさ(ワールド単位)を変えるだけで、建物・住民・道幅・移動の速さなどが、まとめて同じ比率で大きく(小さく)なる。
// 建物・飾りの大きさは、1マスに収まるよう自動で決まる(BuildController/PropPlacer)ので、ここの値に自然に従う。
// マスの数(今は20x20)は別に決める。マスの数を変えずに、マスだけ大きくすると、フィールド全体も同じ比率で広がり、
// 建物や住民は、その分大きく映る(カメラの最初の距離は変えないので、見えるマスの数が減る)。
// カメラの一番引ける距離だけは、フィールドの広さに合わせる(下のkCamera...)。
namespace WorldScale
{
    // 1マスの一辺(ワールド単位)。最初は1.0だった(古いセーブデータは、この大きさで保存されている)。
    constexpr float kCellSize = 1.0f;

    // フィールドのマスの数。
    constexpr int kFieldWidthCells = 20;
    constexpr int kFieldDepthCells = 20;

    // 古いセーブデータにマスの大きさの記録が無い時の値(記録を足す前は、全てこの大きさだった)。
    constexpr float kLegacyCellSize = 1.0f;

    // マスが1.0だった頃に決めた長さ(道幅・速さ・距離のしきい値など)を、今のマスの大きさに合わせる。
    // 「マス基準」で決めた値(マスの何倍か)は、この関数を通せば、マスの大きさが変わっても同じ比率を保てる。
    constexpr float FromLegacy(float legacyLength)
    {
        return legacyLength * (kCellSize / kLegacyCellSize);
    }

    // 画面の遠さ(カメラの描画の奥行き。ニア/ファー)。フィールドが大きくなっても、端が欠けないようにする。
    constexpr float kCameraNearPlane = 0.1f;
    constexpr float kCameraFarPlane = 400.0f;

    // 街づくりのカメラの距離(注視点からカメラまで)。
    // 最初のズームと一番寄る距離は、ワールド単位の「絶対」の値(最初のマス1.0の時のカメラと同じ)。マスを大きくしても変えないので、
    // 同じ距離から見ると、建物や住民が、マスの大きさの比だけ大きく映る。
    constexpr float kCameraDefaultDistance = 14.0f;
    constexpr float kCameraMinDistance = 4.0f;
    // 一番引ける距離だけは、フィールドの幅の何倍か。世界が大きくなっても、一番引けば全体が見えるようにする(幅20の時の40と同じ比率)。
    constexpr float kCameraMaxDistanceFieldRatio = 2.0f;
}

#pragma once
#include <vector>
#include <DirectXMath.h>

// 自由描き(Freehand)の道路の、点列をきれいにする部品。
// マウスで引いた線は、手ぶれで細かく揺れ、点の間隔もバラバラになる。そのまま道路にすると、ガタガタに見える。
// そこで、(1) 揺れを均し(ガウス関数の重みの移動平均)、(2) 等間隔の点に取り直す。
// 始点と終点は、動かさない(既存の道路への吸着・接続の位置が、ずれないように)。始点・終点での向きも保たれる(端の外側を、点対称に延ばして均すため)。
// 画面・ゲームを知らない(点を渡すと、点が返る)ので、単体テストできる。

struct CurveSmoothingOptions
{
    // 均す強さ(ガウス関数の標準偏差。ワールドの長さ)。大きいほど滑らか(代わりに、曲がり角が丸くなる)。0以下なら、均さない。
    float sigma = 0.8f;

    // 出力の点の間隔(ワールドの長さ)。全体を等分するので、実際の間隔は、これに近い値になる。
    float spacing = 0.8f;
};

// pointsを、滑らかに均して、等間隔に取り直した点列を返す。
//  - 2点未満・長さがほぼ0の入力は、そのまま返す。
//  - 先頭と最後の点は、入力と完全に同じ。
//  - 高さ(y)は、均さず、線形に補う(道路は地面の上なので、実質0)。
std::vector<DirectX::XMFLOAT3> SmoothAndResample(const std::vector<DirectX::XMFLOAT3>& points, const CurveSmoothingOptions& options);

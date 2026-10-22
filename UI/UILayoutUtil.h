#pragma once
#include <cmath>

// UIの配置で共通に使う小さな計算(HUDとメニューで同じ倍率・同じ丸め方を使うため、1か所に置く)。

// 画面の高さに応じた倍率。1080を基準に、小さい画面でも読める0.75〜大きい画面での2.0に収める。
inline float UIScaleForHeight(float screenH)
{
    float scale = screenH / 1080.0f;
    if (scale < 0.75f) { scale = 0.75f; }
    if (scale > 2.0f) { scale = 2.0f; }
    return scale;
}

// 文字サイズは整数ピクセルに丸める(FontAtlasが整数サイズで焼くため、描画と測定で同じ大きさになるように)。
inline float UIRoundFontSize(float size)
{
    float rounded = std::floor(size + 0.5f);
    return (rounded < 1.0f) ? 1.0f : rounded;
}

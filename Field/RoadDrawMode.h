#pragma once

// 道路配置ツールの引き方。
// Straight: 従来通りクリック2点で直線を引く(ズレなく正確に引きたい時用)。
// Curve: ドラッグでフリーハンドに曲線を引く(内部的には短い直線の連なりとして扱う)。
enum class RoadDrawMode
{
    Straight,
    Curve
};

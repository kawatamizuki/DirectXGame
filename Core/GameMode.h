#pragma once

// ========================================
// GameMode
// ========================================
//
// Editing : DebugEditorのオブジェクト選択・ギズモ操作が有効
// Playing : BuildController/RoadSystemなどのゲームプレイ入力が有効
//
// GameContext::mode を切り替えることで、
// 左クリックが「編集操作」と「ゲームプレイ操作」のどちらに使われるかを決める。
// ========================================
enum class GameMode
{
    Editing,
    Playing
};

#pragma once
#include <string>

// Assetsパネルのモデルを「ゲームの画面」へドラッグしている間の状態。
// AssetBrowser(ImGuiのドラッグ&ドロップ)が毎フレーム書き込み、PropPlacer(ゲーム側)が読んで、ゴーストの表示と配置を行う。
// 2つの部品が直接お互いを知らずに済むように、GameContext 経由の受け渡し専用の小さな構造体にしている
// (CostPreview と同じ作法)。
struct AssetDragState
{
    bool active = false;        // モデルをドラッグしている最中か
    std::string path;           // ドラッグしているモデルのプロジェクト相対パス
    bool overGameView = false;  // カーソルが、ImGuiのどのウィンドウの上でもない(=ゲームの画面の上)か
    bool dropped = false;       // このフレームで、ボタンを離したか(ゲームの画面の上で離した時だけtrue)
};

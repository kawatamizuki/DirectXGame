#pragma once

// エディタ(ImGuiのデバッグパネル)が無いビルド(Release)でも、安全に呼べる判定。
// ImGuiは、エディタ(ENABLE_EDITOR)が有効なビルドでだけ初期化される。無いビルドで ImGui::GetIO() などを呼ぶと、
// ImGuiのコンテキストが無く、落ちる。ゲームの動作の中で、「ImGuiのパネルの上にカーソルがあるか」を知りたい時は、これを使う。
#ifdef ENABLE_EDITOR
#include "imgui.h"
#endif

// カーソルが、エディタのパネルの上にあるか(エディタが無いビルドでは、常にfalse)。
inline bool IsMouseOverEditorUi()
{
#ifdef ENABLE_EDITOR
    return ImGui::GetIO().WantCaptureMouse;
#else
    return false;
#endif
}

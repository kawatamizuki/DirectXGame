// 単体テスト用の imgui.h の代用品(何もしない)。
// Core/GameClock.cpp が、デバッグ用の「Time」パネルのために ImGui を呼んでいる。
// テストでは、パネルを描かないので、リンクが通るだけの空の実装を用意する。
// 本物の ImGui(imgui/ フォルダ)は、テストには含めない。
#pragma once

struct ImVec2
{
    float x;
    float y;
    ImVec2(float x_ = 0.0f, float y_ = 0.0f) : x(x_), y(y_) {}
};

enum ImGuiCond_
{
    ImGuiCond_FirstUseEver = 4
};

namespace ImGui
{
    inline void SetNextWindowPos(const ImVec2&, int = 0) {}
    inline bool Begin(const char*, bool* = nullptr, int = 0) { return false; }
    inline void End() {}
    inline void Text(const char*, ...) {}
    inline bool RadioButton(const char*, int*, int) { return false; }
    inline void SameLine() {}
    inline bool SliderFloat(const char*, float*, float, float, const char* = nullptr) { return false; }
    inline bool Button(const char*) { return false; }
    inline bool DragFloat(const char*, float*, float = 1.0f, float = 0.0f, float = 0.0f, const char* = nullptr) { return false; }
}

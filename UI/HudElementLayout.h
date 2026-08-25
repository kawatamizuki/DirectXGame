#pragma once

#include <DirectXMath.h>
#include <string>

enum class HudAnchor
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center
};

struct HudElementLayout
{
    std::string id;
    std::string texturePath;
    HudAnchor anchor = HudAnchor::TopLeft;
    DirectX::XMFLOAT2 offset = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 pivot = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 size = { 160.0f, 48.0f };
    float opacity = 0.75f;
    bool visible = true;
};

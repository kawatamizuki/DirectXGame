#pragma once
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

// フィールド上のマス目座標(XZ平面)。
struct GridCoord
{
    int x = 0;
    int z = 0;

    bool operator==(const GridCoord& other) const
    {
        return x == other.x && z == other.z;
    }
};

// unordered_set/unordered_mapでGridCoordをキーにするためのハッシュ。
struct GridCoordHash
{
    size_t operator()(const GridCoord& c) const
    {
        return (static_cast<size_t>(static_cast<uint32_t>(c.x)) << 32) ^
                static_cast<uint32_t>(c.z);
    }
};

// ワールド座標 -> グリッド座標。
// floorfを使うことで負の座標も正しくスナップする
// ((int)キャストだと-0.3fが0に丸まってしまう)。
GridCoord WorldToGrid(const DirectX::XMFLOAT3& worldPos, float cellSize);

// グリッド座標 -> ワールド座標(セル中心、y=0)。
DirectX::XMFLOAT3 GridToWorld(const GridCoord& coord, float cellSize);

// グリッド座標 -> ワールド座標(セル最小角、y=0)。枠線描画に使う。
DirectX::XMFLOAT3 GridToWorldCorner(const GridCoord& coord, float cellSize);

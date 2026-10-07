#pragma once
#include <DirectXMath.h>
#include "GridCoord.h"
#include "Ray.h"

class DebugRenderer;
class GridOrientationRegistry;

// 建物・道路を配置するグリッド地面。
// 占有データ(建てられているかどうか)は持たない(IOccupancyGridに分離)。
class Field
{
public:
    void Initialize(int widthCells, int depthCells, float cellSize);

    int GetWidthCells() const { return m_widthCells; }
    int GetDepthCells() const { return m_depthCells; }
    float GetCellSize() const { return m_cellSize; }

    // ワールド空間でのField全体のサイズ(X方向/Z方向)。
    // 地面モデルなどの見た目をFieldの実サイズに自動で合わせる時に使う。
    float GetWorldWidth() const { return static_cast<float>(m_widthCells) * m_cellSize; }
    float GetWorldDepth() const { return static_cast<float>(m_depthCells) * m_cellSize; }

    bool IsInBounds(const GridCoord& coord) const;

    // 地面のグリッド線を描画する(既存のDebugRenderer::AddLineを使う)。
    // orientationRegistryで向きが確定しているチャンクは、そのローカル座標系に沿った
    // 線を追加で描き、道路に揃った向きが目で見て分かるようにする
    // (ワールド軸のままのチャンクは何もしないので、既存の見た目は変わらない)。
    void DrawGridOverlay(DebugRenderer& debugRenderer, const GridOrientationRegistry& orientationRegistry) const;

    // 地面の高さ・レイキャストをここに集約する。
    // v1では平面(y=0)固定だが、呼び出し側(BuildController/RoadSystem)は
    // 「地面が平面である」ことを直接知らない形にしておく。
    // 将来ハイトマップ地形を導入する際、この中身だけを差し替えればよい。
    float GetHeightAt(float worldX, float worldZ) const;
    bool RaycastGround(const Ray& ray, DirectX::XMFLOAT3& outHitPoint) const;

private:
    int m_widthCells = 20;
    int m_depthCells = 20;
    float m_cellSize = 1.0f;
};

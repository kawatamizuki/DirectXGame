#pragma once
#include "IRoadPlacementStrategy.h"

// IRoadPlacementStrategyのv1実装。
// 1クリック目で始点を保持、2クリック目で直線のRoadSegmentを確定する。
class StraightRoadPlacementStrategy : public IRoadPlacementStrategy
{
public:
    std::optional<RoadSegment> OnPlacementPoint(const DirectX::XMFLOAT3& worldPoint) override;
    void Reset() override;
    bool HasPendingStart(DirectX::XMFLOAT3& outStart) const override;

private:
    bool m_hasStart = false;
    DirectX::XMFLOAT3 m_start{};
    float m_defaultWidth = 1.0f;
};

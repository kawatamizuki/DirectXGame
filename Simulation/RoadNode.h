#pragma once
#include <vector>
#include <cstddef>
#include <DirectXMath.h>

// 交差点・端点の種別。接続している道路区間の本数から自動で決まる。
enum class RoadNodeType
{
    DeadEnd,   // 1本のみ(行き止まり)
    Through,   // 2本(通過点。T字路分岐の途中など)
    TJunction, // 3本
    Crossroad  // 4本以上
};

// 道路網上の1つの分岐点/端点を表すデータ。
// RoadSystemが常時保持する(道路を1本置くたびに差分更新される)。
// 将来、建物を道路の向きに合わせて回転させる機能や、
// 交差点/道路種別に応じたパラメータ補正の問い合わせ先として使う想定。
struct RoadNode
{
    DirectX::XMFLOAT3 position{};
    std::vector<size_t> connectedSegmentIndices; // RoadSystem::m_segmentsへの添字

    RoadNodeType GetType() const;
};

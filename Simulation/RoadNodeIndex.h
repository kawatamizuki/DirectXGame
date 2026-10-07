#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "RoadNode.h"

// 座標を粗いグリッドに区切ったバケツごとに、そこに属するRoadNodeのidだけを保持する空間ハッシュ。
// 「この座標と一致する既存ノードは無いか」という問い合わせを、全ノードへの線形探索ではなく
// 該当バケツとその隣接バケツの少数の候補だけを見て済ませることで、ノード数が増えても
// 1回の問い合わせ・登録コストをほぼ一定に保つ(RoadGraph::BuildFromSegmentsの
// 線形探索と違い、道路網全体の規模に依存しない)。
class RoadNodeIndex
{
public:
    // pointと一致(誤差1mm以内)する既存ノードのidを返す。無ければ-1。
    int FindNode(const DirectX::XMFLOAT3& point, const std::vector<RoadNode>& nodes) const;

    // 新規ノードのidを登録する(FindNodeで見つからなかった時に呼ぶ)。
    void RegisterNode(const DirectX::XMFLOAT3& point, int nodeId);

private:
    // バケツのキー(粗いグリッド座標)。
    struct GridKey
    {
        int x, y, z;
        bool operator==(const GridKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct GridKeyHash
    {
        size_t operator()(const GridKey& key) const
        {
            size_t h = static_cast<size_t>(static_cast<uint32_t>(key.x)) * 73856093u;
            h ^= static_cast<size_t>(static_cast<uint32_t>(key.y)) * 19349663u;
            h ^= static_cast<size_t>(static_cast<uint32_t>(key.z)) * 83492791u;
            return h;
        }
    };

    static constexpr float kBucketSize = 0.05f;       // バケツ1個の1辺の大きさ(5cm四方)
    static constexpr float kSameNodeEpsilon = 0.001f; // 同一ノードとみなす距離(1mm)

    GridKey ToGridKey(const DirectX::XMFLOAT3& point) const;

    // キー→そのバケツに属するノードidの一覧
    std::unordered_map<GridKey, std::vector<int>, GridKeyHash> m_buckets;
};

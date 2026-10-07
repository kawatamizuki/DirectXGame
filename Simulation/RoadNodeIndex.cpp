#include <cmath>
#include "RoadNodeIndex.h"

using namespace DirectX;

RoadNodeIndex::GridKey RoadNodeIndex::ToGridKey(const XMFLOAT3& point) const
{
    return GridKey
    {
        static_cast<int>(std::floor(point.x / kBucketSize)),
        static_cast<int>(std::floor(point.y / kBucketSize)),
        static_cast<int>(std::floor(point.z / kBucketSize))
    };
}

int RoadNodeIndex::FindNode(const XMFLOAT3& point, const std::vector<RoadNode>& nodes) const
{
    GridKey centerKey = ToGridKey(point);
    XMVECTOR pointVec = XMLoadFloat3(&point);
    float epsilonSq = kSameNodeEpsilon * kSameNodeEpsilon;

    // 誤差(1mm)がバケツの境界をまたぐ可能性があるため、中心バケツだけでなく
    // 隣接する3x3x3個のバケツすべての候補を確認する。バケツ1個あたりの候補数は
    // 常にごく少数なので、これでも道路網全体の規模には依存しない。
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                GridKey neighborKey{ centerKey.x + dx, centerKey.y + dy, centerKey.z + dz };

                auto it = m_buckets.find(neighborKey);
                if (it == m_buckets.end())
                {
                    continue;
                }

                for (int candidateId : it->second)
                {
                    XMVECTOR candidateVec = XMLoadFloat3(&nodes[candidateId].position);
                    float distSq = XMVectorGetX(XMVector3LengthSq(candidateVec - pointVec));
                    if (distSq <= epsilonSq)
                    {
                        return candidateId;
                    }
                }
            }
        }
    }

    return -1;
}

void RoadNodeIndex::RegisterNode(const XMFLOAT3& point, int nodeId)
{
    GridKey key = ToGridKey(point);
    m_buckets[key].push_back(nodeId);
}

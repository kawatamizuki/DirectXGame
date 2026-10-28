#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include "RoadCutting.h"

// ---- 解体の、選択と費用の計算(純粋な部分。画面・ゲームの状態を知らない。単体テストできる) ----

// 建物1棟の解体費(円)。建物の種類にかかわらず、一律。
constexpr int kBuildingDemolitionCost = 100;

// 解体ツールを選べるか(メニューで「解体」を押した時の確認)。解体に必要な最小の金額(道路の最小の費用)を持っていれば、選べる。
// 建物の解体(100円)には足りなくても、道路は、短ければ、解体できるので、道路の最小の費用で判断する。0円・マイナスなら、選べない。
// (選んだ後で、実際の長さの費用が払えない時は、従来どおり、解体を取り消して、理由を出す)
inline bool CanSelectDemolitionTool(int cash)
{
    return cash >= kMinimumRoadDemolitionCost;
}

// 今の所持金で、cost円を払えるか(足りなければ、解体できない。残金は、負にならない)。
inline bool CanPayDemolition(int cash, int cost)
{
    return cost >= 0 && cost <= cash;
}

// ドラッグで選んだ建物の集まり。同じ建物を、何度なぞっても、1回しか入らない(多重選択・多重請求の防止)。
// 建物は、GameObject::id(消しても変わらない番号)で持つ。添字やポインタは持たない。
class DemolitionSelection
{
public:
    // idを足す。新しく入ったらtrue、既に入っている・0(無効)ならfalse。
    bool Add(uint32_t id)
    {
        if (id == 0 || Contains(id))
        {
            return false;
        }
        m_ids.push_back(id);
        return true;
    }

    bool Contains(uint32_t id) const { return std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end(); }
    void Clear() { m_ids.clear(); }
    bool Empty() const { return m_ids.empty(); }
    size_t Count() const { return m_ids.size(); }
    const std::vector<uint32_t>& Ids() const { return m_ids; }

    // 選んだ建物の解体費の合計。
    int BuildingCost() const { return static_cast<int>(m_ids.size()) * kBuildingDemolitionCost; }

private:
    std::vector<uint32_t> m_ids;    // 選んだ順
};

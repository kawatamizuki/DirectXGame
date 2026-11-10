#pragma once
#include <vector>
#include "CommercialArea.h"
#include "ISaveable.h"
#include "IAreaSource.h"
#include "AreaLedger.h"
#include "AreaReputation.h"
#include <unordered_map>

// 商業エリアの区画を持ち、仮置き・移動・削除・周知を行う(P2-A)。ISaveableなので、"commercialAreas" の区画として保存・復元される。
//  ・区画の操作は、ゲームのオブジェクト・画面を知らない(位置と週を渡すだけ)。画面の操作は Field/AreaToolController が行う。
//  ・所属(どの施設がどの区画か)は、保存せず、Refresh で、今の建物の敷地から求め直す(建築・解体・区画の移動に、自動で追従する)。
//  ・住民の行動・売上・評判には、まだ、何も影響しない(P2-A)。周知後の猶予・評判は P2-D、エリア目的の外出は P2-C。
//  ・乱数を使わない。
class CommercialAreaManager : public ISaveable, public IAreaSource
{
public:
    CommercialAreaSettings& Settings() { return m_settings; }
    const CommercialAreaSettings& Settings() const { return m_settings; }

    const std::vector<CommercialArea>& Areas() const { return m_areas; }

    // ---- 読み取り専用の窓口(IAreaSource。住民のシミュレーションが読む) ----
    const std::vector<CommercialArea>& GetAreas() const override { return m_areas; }
    const CommercialAreaSettings& GetAreaSettings() const override { return m_settings; }
    const CommercialArea* Find(uint32_t id) const;
    const std::vector<WithdrawnAreaRecord>& Withdrawn() const { return m_withdrawn; }

    // 区画の一辺(ワールドの長さ。設定の「マス基準」の長さを、今の長さの基準に直した値)。
    float SideLength() const;

    // center を中心にした区画を作る(格子 cellSize に隅を合わせる)。作れない(他の区画と重なる)時は、置かずに0を返す。成功したら、新しい区画のID。
    uint32_t PlaceArea(float centerX, float centerZ, float cellSize);

    // center へ動かした場合の矩形。動かせる(他の区画と重ならない)ならtrue。preview表示にも使う。
    bool PreviewRect(uint32_t ignoreId, float centerX, float centerZ, float cellSize, AreaRect& out) const;

    // 区画を center へ動かす。動かせなければfalse(何も変えない)。周知済みなら、履歴に「動かした」を残す(週は currentWeek)。
    bool MoveArea(uint32_t id, float centerX, float centerZ, float cellSize, int currentWeek);

    // 区画を削除する。周知済みなら、撤回の記録(WithdrawnAreaRecord)を残す。無ければfalse。
    bool DeleteArea(uint32_t id, int currentWeek);

    // 区画を周知する(週は currentWeek)。既に周知済み・無ければfalse。周知した時の計画規模を、今の設定の値で固定する。
    bool Publish(uint32_t id, int currentWeek);

    // 今の建物から、所属と達成状態を求め直す。毎フレーム呼んでよい(区画・施設の数は少ない。乱数・保存状態は、達成状態の更新だけ)。
    void Refresh(const std::vector<AreaFacility>& facilities);

    const std::vector<AreaMembers>& Members() const { return m_members; }
    const AreaMembers* MembersOf(uint32_t id) const;

    // 週末の評判の更新(P2-D)。周知した区画だけが変わる(非公開の仮置きは変わらない)。
    // storesByArea: 区画ごとの、今の所属店舗数(住民の行動が使っている所属。画面の更新に依らない)。ledger: 先週の集計(週末の区切りの後)。
    // 達成状態(過去の最高到達)も、ここで、店舗数から引き上げる(画面を描かない試験でも、履歴が残る)。乱数を使わない。
    void UpdateReputations(int currentWeek, const std::unordered_map<uint32_t, int>& storesByArea, const AreaLedger& ledger);

    // 区画の施設構成の評価(P2-B。直近の Refresh の結果。保存しない。住民の行動には、まだ、影響しない)。区画が無ければ、店なしの評価。
    AreaComposition CompositionOf(uint32_t id) const;
    AreaProgress ProgressOf(uint32_t id) const;

    // (x, z) を含む区画のID(無ければ0)。クリックで選ぶのに使う。
    uint32_t FindAreaAt(float x, float z) const;

    // 点検(区画の重なり・ID・二重所属)。問題があれば文章。空なら問題なし。
    std::vector<const char*> Validate() const;

    // メモリの概算(バイト)。
    size_t EstimateBytes() const;

    // ---- セーブ/ロード(ISaveable) ----
    // 保存するのは、区画のID・形・公開状態・周知した週・計画規模・達成状態・履歴と、撤回の記録。所属は保存しない(位置から求める)。設定も保存しない。
    const char* SaveKey() const override { return "commercialAreas"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override;

private:
    CommercialArea* FindMutable(uint32_t id);

    CommercialAreaSettings m_settings;
    std::vector<CommercialArea> m_areas;
    std::vector<WithdrawnAreaRecord> m_withdrawn;
    uint32_t m_nextId = 1;
    std::vector<AreaMembers> m_members;     // 直近の Refresh の結果(保存しない)
    std::vector<AreaComposition> m_compositions;    // m_members と同じ並びの、施設構成の評価(保存しない)
};

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class DemandSystem;
class GameObject;
class SimStats;
struct SimStatBucket;
struct SimStatsScopeData;

// Editorの「行動統計」パネル。住民全体・施設別・住民別・週ごとの、外出や立ち寄りの件数と率を見せる(段階4)。
//
//  ・データは DemandSystem::GetStats()(SimStats)から読むだけ。ここで数え直さない・乱数を引かない。
//  ・率は、必ず「分母」を並べて出す。件数が少ない(30未満)時は「参考」と明記し、95%の範囲(Wilson)も出す。率だけで断定しない。
//  ・「通常」と「テスト」(行動テストで強制した流れ)は、別々に出す。強制の結果で、通常のバランスの数字を汚さない。
//  ・不成立の理由は、排他的な「最初の不成立の理由」で出す(1回の外出・立ち寄りが、2つの理由に重複して数えられない)。
//  ・ENABLE_EDITOR のビルドだけが呼ぶ(Releaseの画面には出さない)。
class SimStatsPanel
{
public:
    // ImGuiのウィンドウの開閉フラグ(EditorUiが持つ)。nullptrなら常に開く。
    void SetOpenFlag(bool* open) { m_open = open; }

    void Draw(DemandSystem& demand, const std::vector<GameObject>& objects);

private:
    void DrawSummary(DemandSystem& demand);
    void DrawFlows(const SimStatsScopeData& scope, const SimStatBucket& bucket, bool normalScope);
    void DrawWeekly(const SimStatsScopeData& scope, int currentWeek);
    void DrawFacilities(const SimStatsScopeData& scope, const std::vector<GameObject>& objects);
    void DrawResidents(const SimStatsScopeData& scope, int currentWeek);
    // 表示中の範囲・期間の統計を、CSV(文字列)にする。
    std::string BuildCsv(const SimStats& stats, const std::vector<GameObject>& objects) const;

    bool* m_open = nullptr;
    int m_scopeSelect = 0;      // 0=通常 1=テスト 2=並べて
    int m_periodSelect = 0;     // 0=通算 1=今週 2=先週
    int m_facilitySort = 0;     // 0=利用回数 1=売上 2=選ばれた回数 3=候補に入った回数
    int m_residentSort = 0;     // 0=外出 1=利用 2=使ったお金 3=立ち往生
};

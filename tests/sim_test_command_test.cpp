// 段階3: 行動テストの指示(SimTest)の、対象の選び方と、記録へのテスト番号の付与の単体テスト。
#include <cstdio>
#include <vector>
#include "SimDebugRecorder.h"
#include "SimTest.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static std::vector<SimTestResidentPoint> Residents()
{
    return {
        { 10, 0.0f, 0.0f },
        { 11, 5.0f, 5.0f },
        { 12, 10.0f, 10.0f },
        { 13, -3.0f, 4.0f },
    };
}

int main()
{
    const auto residents = Residents();

    // ---- 対象の選び方 ----
    {
        SimTestCommand c;
        c.target = SimTestTarget::Selected;
        bool truncated = true;
        CHECK(SelectTestTargets(residents, c, 11, &truncated) == std::vector<uint32_t>{ 11 } && !truncated);
        CHECK(SelectTestTargets(residents, c, 0).empty());          // 選択なし
        CHECK(SelectTestTargets(residents, c, 999).empty());        // 消えた住民は対象にしない

        c.target = SimTestTarget::All;
        CHECK(SelectTestTargets(residents, c, 0) == std::vector<uint32_t>({ 10, 11, 12, 13 }));   // 並びを保つ

        c.target = SimTestTarget::Rect;
        c.rectMinX = 0.0f; c.rectMaxX = 5.0f; c.rectMinZ = 0.0f; c.rectMaxZ = 5.0f;
        CHECK(SelectTestTargets(residents, c, 0) == std::vector<uint32_t>({ 10, 11 }));            // 境界を含む
        c.rectMinX = 5.0f; c.rectMaxX = -5.0f; c.rectMinZ = 5.0f; c.rectMaxZ = -5.0f;              // min>max でも入れ替えて扱う
        CHECK(SelectTestTargets(residents, c, 0) == std::vector<uint32_t>({ 10, 11, 13 }));
    }

    // ---- 人数の上限 ----
    {
        SimTestCommand c;
        c.target = SimTestTarget::All;
        c.maxCount = 2;
        bool truncated = false;
        CHECK(SelectTestTargets(residents, c, 0, &truncated) == std::vector<uint32_t>({ 10, 11 }) && truncated);
        c.maxCount = 0;                                             // 1以上に収める
        CHECK(SelectTestTargets(residents, c, 0).size() == 1);
        c.maxCount = 4;                                             // ちょうどなら、切り詰めない
        truncated = true;
        CHECK(SelectTestTargets(residents, c, 0, &truncated).size() == 4 && !truncated);

        std::vector<SimTestResidentPoint> many;
        for (uint32_t i = 1; i <= static_cast<uint32_t>(kSimTestMaxBatch) + 50; ++i) { many.push_back({ i, 0.0f, 0.0f }); }
        c.maxCount = 100000;                                        // 絶対の上限に収める
        truncated = false;
        CHECK(SelectTestTargets(many, c, 0, &truncated).size() == static_cast<size_t>(kSimTestMaxBatch) && truncated);
    }

    // ---- 計画の状態 ----
    {
        CHECK(SimTestIsActive(SimTestStatus::Pending));
        CHECK(SimTestIsActive(SimTestStatus::Running));
        CHECK(!SimTestIsActive(SimTestStatus::Succeeded) && !SimTestIsActive(SimTestStatus::Rejected) && !SimTestIsActive(SimTestStatus::Cancelled) && !SimTestIsActive(SimTestStatus::Expired));
    }

    // ---- 記録へのテスト番号の付与 ----
    {
        SimDebugRecorder rec;
        rec.SetEnabled(true);
        SimEvent e;
        e.residentId = 1;
        e.kind = SimEventKind::OutingRoll;
        rec.AddEvent(e);
        CHECK(rec.Events().back().testId == 0);                    // 通常のプレイ

        rec.SetActiveTest(1, 7);
        CHECK(rec.ActiveTestId(1) == 7 && rec.ActiveTestId(2) == 0);
        rec.AddEvent(e);
        CHECK(rec.Events().back().testId == 7);                    // その住民のイベントに付く
        SimEvent other = e;
        other.residentId = 2;
        rec.AddEvent(other);
        CHECK(rec.Events().back().testId == 0);                    // 他の住民には付かない

        SimDecisionDetail detail;
        detail.residentId = 1;
        const uint64_t detailId = rec.AddDetail(detail);
        CHECK(rec.FindDetail(detailId) && rec.FindDetail(detailId)->testId == 7);

        rec.SetActiveTest(1, 0);                                    // 外す
        rec.AddEvent(e);
        CHECK(rec.Events().back().testId == 0);

        rec.SetEnabled(false);                                      // OFFの間は、何もしない
        rec.SetActiveTest(1, 9);
        CHECK(rec.ActiveTestId(1) == 0);
    }

    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

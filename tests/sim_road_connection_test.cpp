// 建物と道路の接続判定(敷地の縁で測る)の単体テスト。
// 不具合: 判定が建物の「中心」から道路までの距離だったため、大きな建物(映画館 2×2)は、隙間ゼロで隣接しても、中心が遠くなって「道がない」になった。
// 受け入れ条件:
//   1. 2×2の建物が、道路に隣接(隙間0)していれば、どの向きでも接続できる。丸め誤差(わずかな隙間)でも接続できる。
//   2. 許す隙間は、従来と同じ(0.5マスまで)。1マス離れたら、接続しない(大きさに依らない)。
//   3. 1×1の建物の結果は、従来(中心から1.5)と同じ。
//   4. 接続点(入口/私道の端)は、道路上の、敷地に一番近い点。
//   5. 道路が敷地を貫いている場合は、距離0。
#include <cmath>
#include <cstdio>
#include <vector>
#include "RoadProximity.h"
#include "WorldScale.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static const float kEdgeLimit = WorldScale::FromLegacy(1.0f) + WorldScale::FromLegacy(0.01f);   // DemandSystem の kAdjacentEdgeDistance と同じ

static float L(float legacy) { return WorldScale::FromLegacy(legacy); }

// x軸に沿った、道路(中心線 z=0、道幅1マス)。
static std::vector<RoadSegment> MakeRoad()
{
    RoadSegment s;
    s.start = { L(-10.0f), 0.0f, 0.0f };
    s.end = { L(10.0f), 0.0f, 0.0f };
    s.width = L(1.0f);
    return { s };
}

// yaw=0 の矩形: 最小角 (x0, z0)、幅 w(+X)、奥行 d(+Z)(マス基準)。
static OrientedRect Rect(float x0, float z0, float w, float d, float yaw = 0.0f)
{
    OrientedRect r;
    r.origin = { L(x0), 0.0f, L(z0) };
    r.yaw = yaw;
    r.width = L(w);
    r.depth = L(d);
    return r;
}

static bool Connects(const OrientedRect& rect)
{
    return FindNearestRoadToRect(MakeRoad(), rect, kEdgeLimit).has_value();
}

static void TestLargeBuilding()
{
    // 映画館(2×2)。道路の縁は z=0.5。隙間ゼロで隣接(敷地の縁が z=0.5)。
    CHECK(Connects(Rect(-1.0f, 0.5f, 2.0f, 2.0f)));
    // 丸め誤差程度のわずかな隙間(従来は、ここで「道がない」になった)。
    CHECK(Connects(Rect(-1.0f, 0.5f + 0.001f, 2.0f, 2.0f)));
    CHECK(Connects(Rect(-1.0f, 0.5f + 0.009f, 2.0f, 2.0f)));
    // 反対側(道路の下側)に隣接。
    CHECK(Connects(Rect(-1.0f, -2.5f, 2.0f, 2.0f)));
    // 許す隙間(0.5マス)。
    CHECK(Connects(Rect(-1.0f, 1.0f, 2.0f, 2.0f)));
    // 1マス離れたら、接続しない(大きさに依らない)。
    CHECK(!Connects(Rect(-1.0f, 1.5f, 2.0f, 2.0f)));
    CHECK(!Connects(Rect(-1.0f, 3.0f, 2.0f, 2.0f)));
    // 向きを変えても(2×2は正方形)、結果は同じ。90度・180度・270度(yawが違っても、同じ位置の矩形)。
    for (int step = 0; step < 4; ++step)
    {
        const float yaw = 1.5707964f * static_cast<float>(step);
        // 位置が同じ矩形を、yawの規約で表す(最小角が変わる)。ここでは、道路に隣接する矩形を、yawごとに作り直す。
        OrientedRect r;
        r.yaw = yaw;
        r.width = L(2.0f);
        r.depth = L(2.0f);
        // yaw 0: 最小角(-1, 0.5)。90度: ローカル+X→(0,-1)、ローカル+Z→(1,0)。矩形が x∈[-1,1], z∈[0.5,2.5] になる最小角。
        const float c = std::cos(yaw), s = std::sin(yaw);
        // 目的の矩形の中心 (0, 1.5) から、最小角 = 中心 - (c,-s)*1 - (s,c)*1 。
        r.origin = { L(0.0f - (c * 1.0f + s * 1.0f)), 0.0f, L(1.5f - (-s * 1.0f + c * 1.0f)) };
        CHECK(Connects(r));
    }
}

static void TestSmallBuildingUnchanged()
{
    // 1×1: 従来(中心から1.5)と同じ結果。隣接(隙間0)・隙間0.5は接続、隙間1.0は接続しない。
    CHECK(Connects(Rect(-0.5f, 0.5f, 1.0f, 1.0f)));
    CHECK(Connects(Rect(-0.5f, 1.0f, 1.0f, 1.0f)));
    CHECK(!Connects(Rect(-0.5f, 1.5f, 1.0f, 1.0f)));
    // 2×1(スーパー)。長辺が道路に平行・垂直のどちらでも、隣接なら接続。
    CHECK(Connects(Rect(-1.0f, 0.5f, 2.0f, 1.0f)));     // 長辺が道路に平行
    CHECK(Connects(Rect(-0.5f, 0.5f, 1.0f, 2.0f)));     // 長辺が道路に垂直(中心が1.5離れる。従来は境界)
    CHECK(!Connects(Rect(-0.5f, 1.5f, 1.0f, 2.0f)));
}

static void TestConnectionPoint()
{
    const auto roads = MakeRoad();
    // 隣接した矩形(x∈[2,4])。接続点は、道路上(z=0)で、敷地の真下(x∈[2,4])。距離は、敷地の縁から中心線まで=0.5。
    const auto hit = FindNearestRoadToRect(roads, Rect(2.0f, 0.5f, 2.0f, 2.0f), kEdgeLimit);
    CHECK(hit.has_value());
    if (hit)
    {
        CHECK(std::fabs(hit->point.z) < 1e-5f);
        CHECK(hit->point.x >= L(2.0f) - 1e-4f && hit->point.x <= L(4.0f) + 1e-4f);
        CHECK(std::fabs(hit->distance - L(0.5f)) < 1e-4f);
        CHECK(hit->segmentIndex == 0);
    }
    // 道路の端(x=10)より外にはみ出す位置の建物: 接続点は、道路の端。
    const auto edge = FindNearestRoadToRect(roads, Rect(10.0f, 0.5f, 2.0f, 2.0f), kEdgeLimit);
    CHECK(edge.has_value() && edge->point.x <= L(10.0f) + 1e-4f);
    // 道路の端からの距離が遠ければ接続しない。
    CHECK(!FindNearestRoadToRect(roads, Rect(12.0f, 0.5f, 2.0f, 2.0f), kEdgeLimit).has_value());
}

static void TestRoadThroughFootprint()
{
    // 道路が敷地を貫いている(置けないはずの配置だが、古いデータ・編集の途中で起こり得る)。距離0で、接続する。
    const auto roads = MakeRoad();
    const auto hit = FindNearestRoadToRect(roads, Rect(-1.0f, -1.0f, 2.0f, 2.0f), kEdgeLimit);
    CHECK(hit.has_value() && hit->distance < 1e-5f);
    // 道路が空なら、見つからない。
    CHECK(!FindNearestRoadToRect({}, Rect(0.0f, 0.0f, 1.0f, 1.0f), kEdgeLimit).has_value());
}

static void TestDiagonalRoad()
{
    // 斜めの道路(45度)でも、隣接した建物は接続する。
    RoadSegment s;
    s.start = { L(-10.0f), 0.0f, L(-10.0f) };
    s.end = { L(10.0f), 0.0f, L(10.0f) };
    s.width = L(1.0f);
    const std::vector<RoadSegment> roads = { s };
    // 道路 z=x。矩形 x∈[2,4], z∈[4.4,6.4]: 角(4,4.4)から線までの距離 = |4-4.4|/√2 ≒ 0.28。
    CHECK(FindNearestRoadToRect(roads, Rect(2.0f, 4.4f, 2.0f, 2.0f), kEdgeLimit).has_value());
    // 十分に離れていれば、接続しない。
    CHECK(!FindNearestRoadToRect(roads, Rect(2.0f, 8.0f, 2.0f, 2.0f), kEdgeLimit).has_value());
}

int main()
{
    TestLargeBuilding();
    TestSmallBuildingUnchanged();
    TestConnectionPoint();
    TestRoadThroughFootprint();
    TestDiagonalRoad();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}

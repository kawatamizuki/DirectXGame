// P2-A(商業エリアの計画区画と周知)のルールの単体テスト。
// 受け入れ条件(仕様書 §10 の5〜8・12の前半・14・15):
//   5. 店のない非公開区画を置いても、周知期限・達成状態は変わらない。移動・取り消しできる。
//   6. 既存の施設群を、後から区画指定できる(所属は、建物の位置から求める。建物・道路は変えない)。
//   7. 周知しただけでは、何も配分しない(区画は、店の数を数えるだけ)。
//   8. 店舗を増やせば進捗は増える(計画の達成率と、実際の魅力は別物。魅力は P2-C 以降)。
//  14. 隣接する区画で、施設の二重所属が起きない。
//  15. 保存→ロードで、区画・公開状態・周知した週・達成状態が一致する。古いセーブ(区画なし)も読める。
// 制作者の決定: 施設の所属は、建物の敷地面積の50%以上が区画の中にある時(中心の位置では判定しない。しきい値は設定)。
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "CommercialAreaManager.h"
#include "WorldScale.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// 軸に沿った敷地(ワールド): x〜x+w, z〜z+d。
static OrientedRect Axis(float x, float z, float w, float d)
{
    OrientedRect r; r.origin = { x, 0.0f, z }; r.yaw = 0.0f; r.width = w; r.depth = d; return r;
}

static AreaFacility Fac(uint32_t id, BuildingType type, const OrientedRect& footprint)
{
    AreaFacility f; f.objectId = id; f.type = type; f.footprint = footprint; return f;
}

static CommercialArea MakeArea(uint32_t id, float minX, float minZ, float maxX, float maxZ)
{
    CommercialArea a; a.id = id; a.rect = { minX, minZ, maxX, maxZ }; return a;
}

// ---- 重なる面積 ----
static void TestOverlapArea()
{
    const AreaRect rect = { 0.0f, 0.0f, 8.0f, 8.0f };
    CHECK(Near(OverlapArea(Axis(2, 2, 2, 2), rect), 4.0f));            // 全部、中
    CHECK(Near(OverlapRatio(Axis(2, 2, 2, 2), rect), 1.0f));
    CHECK(Near(OverlapArea(Axis(7, 2, 2, 2), rect), 2.0f));            // 半分、中(辺をまたぐ)
    CHECK(Near(OverlapRatio(Axis(7, 2, 2, 2), rect), 0.5f));
    CHECK(Near(OverlapArea(Axis(20, 20, 2, 2), rect), 0.0f));          // 外
    CHECK(Near(OverlapArea(Axis(8, 0, 2, 2), rect), 0.0f));            // 辺が接するだけ
    CHECK(Near(OverlapRatio(Axis(7, 7, 2, 2), rect), 0.25f));          // 角
    CHECK(Near(OverlapArea(Axis(-5, -5, 30, 30), rect), 64.0f));       // 区画より大きい敷地(区画の面積が、重なり)
    CHECK(OverlapArea(Axis(0, 0, 0, 2), rect) == 0.0f && OverlapRatio(Axis(0, 0, 0, 2), rect) == 0.0f);   // 面積0

    // 向き(yaw)がある敷地: 45度に回した正方形(一辺2。対角線の長さ 2√2)。中心が区画の辺の上なら、ちょうど半分。
    const float s = 2.0f;
    OrientedRect rotated; rotated.yaw = 3.14159265f / 4.0f; rotated.width = s; rotated.depth = s;
    // 中心 = 原点 + (axisX*w + axisZ*d)/2。axisX=(cos,-sin)、axisZ=(sin,cos)。
    const float cx = 8.0f, cz = 4.0f;     // 区画の右辺(x=8)の上
    const float c = std::cos(rotated.yaw), sn = std::sin(rotated.yaw);
    rotated.origin = { cx - (c * s + sn * s) * 0.5f, 0.0f, cz - (-sn * s + c * s) * 0.5f };
    CHECK(Near(OverlapRatio(rotated, rect), 0.5f, 1e-3f));
    rotated.origin.x -= 3.0f;             // 中に入れる → 全部
    CHECK(Near(OverlapRatio(rotated, rect), 1.0f, 1e-3f));
}

// ---- 所属: 敷地面積の50%以上(中心では判定しない) ----
static void TestMembership()
{
    CommercialAreaSettings s;           // 50%
    std::vector<CommercialArea> areas = { MakeArea(1, 0, 0, 8, 8) };

    // 敷地 4×2(面積8)。右へ動かして、区画から出ていく。
    auto membersFor = [&](float x, const CommercialAreaSettings& settings)
    {
        std::vector<AreaFacility> f = { Fac(10, BuildingType::Shop, Axis(x, 3, 4, 2)) };
        return ComputeAreaMembers(areas, f, settings)[0].facilityIds.size();
    };
    CHECK(membersFor(2.0f, s) == 1);    // 全部、中(100%)
    CHECK(membersFor(6.0f, s) == 1);    // x 6〜10 → 中は 6〜8 の半分(ちょうど50%)→ 所属
    CHECK(membersFor(6.1f, s) == 0);    // 49.9%(中心 8.1 は区画の外)→ 所属しない
    CHECK(membersFor(5.9f, s) == 1);    // 52.5% → 所属
    CHECK(membersFor(-2.1f, s) == 0);   // 反対側の辺も同じ(x -2.1〜1.9 → 中は 1.9 → 47.5%)
    CHECK(membersFor(-2.0f, s) == 1);   // ちょうど50%
    // しきい値は、設定で変えられる(仮設定)。
    CommercialAreaSettings loose = s; loose.membershipMinOverlap = 0.3f;
    CHECK(membersFor(6.8f, loose) == 1);    // 30% → 所属
    CHECK(membersFor(7.0f, loose) == 0);    // 25% → 所属しない
    CommercialAreaSettings strict = s; strict.membershipMinOverlap = 0.9f;
    CHECK(membersFor(5.0f, strict) == 0 && membersFor(4.0f, strict) == 1);

    // 中心が区画の外でも、50%以上が中なら所属する(中心では判定しない): 細長い敷地 12×2 が、x -2〜10。中は 0〜8 = 8/12 = 67%、中心 x=4 は中。
    // 区画の端に長い敷地: x 5〜17(幅12)→ 中は 5〜8 = 25%、中心 11 は外 → 所属しない。
    std::vector<AreaFacility> wide = { Fac(11, BuildingType::Cafe, Axis(5, 3, 12, 2)), Fac(12, BuildingType::Cafe, Axis(-3, 3, 12, 2)) };
    const auto m = ComputeAreaMembers(areas, wide, s)[0];
    CHECK(m.facilityIds.size() == 1 && m.facilityIds[0] == 12);     // 中心 x=3 は中。-3〜9 の中は 0〜8 = 8/12 = 67%
    // 中心が外でも所属する例: 区画より小さい区画で、大きな敷地の大半が入る。
    std::vector<CommercialArea> small = { MakeArea(2, 0, 0, 4, 4) };
    std::vector<AreaFacility> corner = { Fac(13, BuildingType::Shop, Axis(-1, -1, 6, 6)) };   // 敷地 -1〜5。中は 0〜4 の 4×4=16 / 36 = 44% → 所属しない
    CHECK(ComputeAreaMembers(small, corner, s)[0].facilityIds.empty());
    std::vector<AreaFacility> corner2 = { Fac(14, BuildingType::Shop, Axis(-0.5f, -0.5f, 5.0f, 5.0f)) };   // 敷地 -0.5〜4.5。中は 16 / 25 = 64%、中心 2.0 は中
    CHECK(ComputeAreaMembers(small, corner2, s)[0].facilityIds.size() == 1);

    // 住宅・オフィスは、対象外(商業施設だけ)。
    std::vector<AreaFacility> mixed = { Fac(1, BuildingType::House, Axis(2, 2, 2, 2)), Fac(2, BuildingType::Office, Axis(2, 2, 2, 2)),
                                        Fac(3, BuildingType::Shop, Axis(2, 2, 2, 2)), Fac(4, BuildingType::Supermarket, Axis(4, 4, 2, 2)),
                                        Fac(5, BuildingType::Cinema, Axis(1, 5, 2, 2)), Fac(6, BuildingType::Cafe, Axis(5, 1, 2, 2)) };
    const auto mm = ComputeAreaMembers(areas, mixed, s)[0];
    CHECK(mm.facilityIds == std::vector<uint32_t>({ 3, 4, 5, 6 }));    // 小さい順
    CHECK(Near(mm.overlapRatios[0], 1.0f));
}

// ---- 二重所属しない ----
static void TestNoDoubleMembership()
{
    CommercialAreaSettings s;
    // 隣接する2つの区画(辺を共有)。
    std::vector<CommercialArea> areas = { MakeArea(1, 0, 0, 8, 8), MakeArea(2, 8, 0, 16, 8) };
    CHECK(!AreaRectsOverlap(areas[0].rect, areas[1].rect, 1e-3f));          // 接するだけは、重ならない

    // ちょうど50% / 50%: IDが小さい区画だけ。
    std::vector<AreaFacility> f = { Fac(10, BuildingType::Shop, Axis(6, 2, 4, 2)) };
    auto r = ComputeAreaMembers(areas, f, s);
    CHECK(r[0].facilityIds.size() + r[1].facilityIds.size() == 1 && r[0].facilityIds.size() == 1);
    // 25% / 75%: 割合が大きい区画だけ。
    f = { Fac(11, BuildingType::Shop, Axis(7, 2, 4, 2)) };      // 7〜11: 区画1 に 1/4、区画2 に 3/4
    r = ComputeAreaMembers(areas, f, s);
    CHECK(r[0].facilityIds.empty() && r[1].facilityIds.size() == 1);
    // どちらにも50%に満たない(3分割に近い)なら、どちらにも所属しない。
    f = { Fac(12, BuildingType::Shop, Axis(5, 2, 6, 2)) };      // 5〜11 → 区画1 に 3/6、区画2 に 3/6(ちょうど50%)
    r = ComputeAreaMembers(areas, f, s);
    CHECK(r[0].facilityIds.size() + r[1].facilityIds.size() == 1);
    f = { Fac(13, BuildingType::Shop, Axis(3, 2, 14, 2)) };     // 3〜17 → 区画1 に 5/14、区画2 に 8/14(57%)
    r = ComputeAreaMembers(areas, f, s);
    CHECK(r[0].facilityIds.empty() && r[1].facilityIds.size() == 1);
    // 並びの順番を変えても、結果は同じ(決定的)。
    std::vector<AreaFacility> many = { Fac(30, BuildingType::Shop, Axis(6, 2, 4, 2)), Fac(20, BuildingType::Cafe, Axis(2, 2, 2, 2)), Fac(25, BuildingType::Cinema, Axis(10, 2, 2, 2)) };
    auto a1 = ComputeAreaMembers(areas, many, s);
    std::reverse(many.begin(), many.end());
    auto a2 = ComputeAreaMembers(areas, many, s);
    CHECK(a1[0].facilityIds == a2[0].facilityIds && a1[1].facilityIds == a2[1].facilityIds);
    CHECK(ValidateAreas(areas, a1, 1e-3f).empty());

    // 点検は、重なる区画・二重所属を見つける。
    std::vector<CommercialArea> bad = { MakeArea(1, 0, 0, 8, 8), MakeArea(2, 4, 0, 12, 8) };
    CHECK(!ValidateAreas(bad, ComputeAreaMembers(bad, {}, s), 1e-3f).empty());
    std::vector<AreaMembers> doubled(2); doubled[0].areaId = 1; doubled[0].facilityIds = { 7 }; doubled[1].areaId = 2; doubled[1].facilityIds = { 7 };
    CHECK(!ValidateAreas(areas, doubled, 1e-3f).empty());
}

// ---- 進捗 ----
static void TestProgress()
{
    CommercialAreaSettings s;   // 8 / 4 / 6
    CHECK(ComputeAreaProgress(0, 8, s).current == AreaMilestone::None);
    CHECK(ComputeAreaProgress(3, 8, s).current == AreaMilestone::None);
    CHECK(ComputeAreaProgress(4, 8, s).current == AreaMilestone::Minimum);
    CHECK(ComputeAreaProgress(5, 8, s).current == AreaMilestone::Minimum);
    CHECK(ComputeAreaProgress(6, 8, s).current == AreaMilestone::Expanded);
    CHECK(ComputeAreaProgress(7, 8, s).current == AreaMilestone::Expanded);
    CHECK(ComputeAreaProgress(8, 8, s).current == AreaMilestone::Complete);
    CHECK(Near(ComputeAreaProgress(4, 8, s).ratio, 0.5f) && Near(ComputeAreaProgress(12, 8, s).ratio, 1.0f));   // 計画達成率は、1に収める
    CHECK(ComputeAreaProgress(2, 0, s).ratio == 0.0f);                                                           // 計画規模0でも、壊れない
    s.minProgressStores = 2; s.expandedRangeStores = 3;                                                          // 数値は、設定
    CHECK(ComputeAreaProgress(2, 8, s).current == AreaMilestone::Minimum && ComputeAreaProgress(3, 8, s).current == AreaMilestone::Expanded);
    CHECK(AreaMilestoneName(AreaMilestone::Complete) != nullptr && AreaHistoryKindName(AreaHistoryKind::Moved) != nullptr);
}

// ---- 操作: 仮置き・移動・削除・周知 ----
static void TestOperations()
{
    CommercialAreaManager m;
    const float cell = WorldScale::kCellSize;
    const float side = m.SideLength();
    CHECK(Near(side, WorldScale::FromLegacy(8.0f)));

    const uint32_t a = m.PlaceArea(10.0f, 10.0f, cell);
    CHECK(a == 1 && m.Areas().size() == 1);
    const CommercialArea* area = m.Find(a);
    CHECK(area && !area->published && Near(area->rect.Width(), side) && Near(area->rect.Depth(), side));
    CHECK(Near(std::fmod(area->rect.minX / cell + 1000.0f, 1.0f), 0.0f, 1e-3f));         // 格子に合っている
    // 重なる場所には、置けない。辺が接する場所には、置ける。
    CHECK(m.PlaceArea(12.0f, 10.0f, cell) == 0 && m.Areas().size() == 1);
    const uint32_t b = m.PlaceArea(10.0f + side, 10.0f, cell);
    CHECK(b == 2 && m.Areas().size() == 2);

    // 未周知: 動かしても、履歴は残らない。動かせる。自分自身とは、重ならない扱い。
    CHECK(m.MoveArea(a, 10.5f, 20.0f, cell, 5));
    CHECK(Near(m.Find(a)->rect.CenterZ(), 20.0f, cell) && m.Find(a)->history.empty());
    CHECK(m.MoveArea(a, 10.0f + side, 10.0f, cell, 5) == false);                          // b と重なる場所へは、動かせない(位置は、そのまま)
    CHECK(Near(m.Find(a)->rect.CenterZ(), 20.0f, cell));
    CHECK(!m.MoveArea(99, 0, 0, cell, 5));
    // 未周知の削除: 記録は残らない(置いただけの区画は、無かったことになる)。
    CHECK(m.DeleteArea(b, 5) && m.Areas().size() == 1 && m.Withdrawn().empty());
    CHECK(!m.DeleteArea(b, 5));
    // IDは、再利用しない。
    const uint32_t c = m.PlaceArea(50.0f, 50.0f, cell);
    CHECK(c == 3);

    // 周知: 周知した週・計画規模が固定される。二重の周知はできない。
    m.Settings().plannedCapacity = 8;
    CHECK(m.Publish(a, 12));
    CHECK(m.Find(a)->published && m.Find(a)->publishedWeek == 12 && m.Find(a)->plannedCapacity == 8 && m.Find(a)->history.size() == 1);
    m.Settings().plannedCapacity = 10;                                                    // 周知した後に設定を変えても、周知した時の規模
    CHECK(m.Find(a)->plannedCapacity == 8 && m.Find(c)->plannedCapacity == 8);
    CHECK(!m.Publish(a, 13) && !m.Publish(99, 13));
    CHECK(m.Publish(c, 13) && m.Find(c)->plannedCapacity == 10);
    // 周知後の移動: 履歴に残る。周知の週は、リセットされない。
    CHECK(m.MoveArea(a, 11.0f, 21.0f, cell, 14));
    CHECK(m.Find(a)->history.size() == 2 && m.Find(a)->history[1].kind == AreaHistoryKind::Moved && m.Find(a)->history[1].week == 14 && m.Find(a)->publishedWeek == 12);
    // 同じ位置へ動かしても、履歴は増えない。
    CHECK(m.MoveArea(a, 11.0f, 21.0f, cell, 15) && m.Find(a)->history.size() == 2);
    // 周知後の削除(撤回): 記録が残る。
    CHECK(m.DeleteArea(c, 20));
    CHECK(m.Withdrawn().size() == 1 && m.Withdrawn()[0].areaId == c && m.Withdrawn()[0].publishedWeek == 13 && m.Withdrawn()[0].withdrawnWeek == 20);

    // 点・猶予。
    CHECK(m.FindAreaAt(m.Find(a)->rect.CenterX(), m.Find(a)->rect.CenterZ()) == a);
    CHECK(m.FindAreaAt(-1000.0f, -1000.0f) == 0);
    CommercialAreaSettings s;
    CHECK(GraceWeeksLeft(*m.Find(a), 12, s) == 8 && GraceWeeksLeft(*m.Find(a), 16, s) == 4 && GraceWeeksLeft(*m.Find(a), 22, s) == -2);
    CommercialArea fresh = MakeArea(9, 0, 0, 1, 1);
    CHECK(GraceWeeksLeft(fresh, 100, s) == s.graceWeeks);        // 周知していなければ、期限は動かない
    CHECK(m.Validate().empty());
}

// ---- 更新: 建物の増減・区画の移動に追従する / 達成状態 / 置いただけでは何も変わらない ----
static void TestRefreshAndAchievement()
{
    CommercialAreaManager m;
    const float cell = WorldScale::kCellSize;
    const uint32_t a = m.PlaceArea(4.0f, 4.0f, cell);         // 区画は 0〜8 付近
    const AreaRect rect = m.Find(a)->rect;
    auto shop = [&](uint32_t id, int i) { return Fac(id, BuildingType::Shop, Axis(rect.minX + 0.5f + (i % 4) * 1.8f, rect.minZ + 0.5f + (i / 4) * 1.8f, 1.5f, 1.5f)); };

    // 既存の施設を、後から区画指定できる(区画を置いた後に、建物の一覧から所属が決まる)。
    std::vector<AreaFacility> facilities;
    for (int i = 0; i < 5; ++i) { facilities.push_back(shop(100 + i, i)); }
    m.Refresh(facilities);
    CHECK(m.MembersOf(a)->facilityIds.size() == 5 && m.ProgressOf(a).storeCount == 5);
    // 非公開: 店があっても、達成状態・期限は変わらない(置いただけでは、何も起きない)。
    CHECK(m.Find(a)->achieved == AreaMilestone::None && !m.Find(a)->published);

    // 周知 → 達成状態が進む。店舗が増えると進捗が増える。
    CHECK(m.Publish(a, 3));
    m.Refresh(facilities);
    CHECK(m.Find(a)->achieved == AreaMilestone::Minimum && m.ProgressOf(a).current == AreaMilestone::Minimum);
    for (int i = 5; i < 8; ++i) { facilities.push_back(shop(100 + i, i)); }
    m.Refresh(facilities);
    CHECK(m.Find(a)->achieved == AreaMilestone::Complete && Near(m.ProgressOf(a).ratio, 1.0f));
    // 解体(施設が減る)→ 今の段階は下がるが、一度届いた達成状態は、残る。
    facilities.resize(3);
    m.Refresh(facilities);
    CHECK(m.ProgressOf(a).current == AreaMilestone::None && m.Find(a)->achieved == AreaMilestone::Complete);
    // 区画を動かすと、所属が追従する(建物は動かさない)。
    m.MoveArea(a, 40.0f, 40.0f, cell, 9);
    m.Refresh(facilities);
    CHECK(m.MembersOf(a)->facilityIds.empty());
    m.MoveArea(a, 4.0f, 4.0f, cell, 10);
    m.Refresh(facilities);
    CHECK(m.MembersOf(a)->facilityIds.size() == 3);
    // 区画を消すと、所属の結果からも消える。
    m.DeleteArea(a, 11);
    m.Refresh(facilities);
    CHECK(m.Members().empty() && m.MembersOf(a) == nullptr && m.ProgressOf(a).storeCount == 0);
}

// ---- 保存・読み込み ----
static CommercialAreaManager RoundTrip(const CommercialAreaManager& source, float spaceScale = 1.0f)
{
    Json section = Json::MakeObject();
    SaveContext saveContext;
    source.Save(section, saveContext);
    std::string text = section.Dump(true);
    Json parsed;
    std::string error;
    CHECK(Json::Parse(text, parsed, error));
    CommercialAreaManager loaded;
    LoadContext loadContext;
    loadContext.spaceScale = spaceScale;
    loaded.Load(parsed, 1, loadContext);
    return loaded;
}

static void TestSaveLoad()
{
    CommercialAreaManager m;
    const float cell = WorldScale::kCellSize;
    const uint32_t a = m.PlaceArea(10.0f, 10.0f, cell);
    const uint32_t b = m.PlaceArea(30.0f, 10.0f, cell);
    const uint32_t c = m.PlaceArea(50.0f, 10.0f, cell);
    m.Publish(a, 7);
    m.MoveArea(a, 11.0f, 10.0f, cell, 8);
    m.Publish(b, 9);
    std::vector<AreaFacility> fac;
    for (int i = 0; i < 4; ++i) { fac.push_back(Fac(1 + i, BuildingType::Shop, Axis(m.Find(a)->rect.minX + 1.0f + i * 1.7f, m.Find(a)->rect.minZ + 1.0f, 1.5f, 1.5f))); }
    m.Refresh(fac);
    m.DeleteArea(b, 12);                                       // 周知済みを撤回 → 記録
    (void)c;

    const CommercialAreaManager loaded = RoundTrip(m);
    CHECK(loaded.Areas().size() == m.Areas().size() && loaded.Withdrawn().size() == 1);
    for (size_t i = 0; i < m.Areas().size(); ++i)
    {
        const CommercialArea& x = m.Areas()[i];
        const CommercialArea& y = loaded.Areas()[i];
        CHECK(x.id == y.id && x.published == y.published && x.publishedWeek == y.publishedWeek && x.plannedCapacity == y.plannedCapacity);
        CHECK(x.achieved == y.achieved && x.history.size() == y.history.size());
        CHECK(Near(x.rect.minX, y.rect.minX, 1e-4f) && Near(x.rect.minZ, y.rect.minZ, 1e-4f) && Near(x.rect.maxX, y.rect.maxX, 1e-4f) && Near(x.rect.maxZ, y.rect.maxZ, 1e-4f));
    }
    CHECK(loaded.Find(a)->achieved == AreaMilestone::Minimum && loaded.Find(a)->history.size() == 2 && loaded.Find(a)->history[1].kind == AreaHistoryKind::Moved);
    CHECK(loaded.Withdrawn()[0].areaId == b && loaded.Withdrawn()[0].withdrawnWeek == 12);
    // IDは、撤回した区画のIDも含めて、再利用しない。
    CommercialAreaManager copy = loaded;
    CHECK(copy.PlaceArea(100.0f, 100.0f, cell) == 4);

    // 長さの基準が変わっていたら(spaceScale)、矩形が、今の大きさに合う。
    const CommercialAreaManager scaled = RoundTrip(m, 2.0f);
    CHECK(Near(scaled.Find(a)->rect.minX, m.Find(a)->rect.minX * 2.0f, 1e-3f) && Near(scaled.Find(a)->rect.Width(), m.Find(a)->rect.Width() * 2.0f, 1e-3f));

    // 古いセーブ(この区画が無い)は、「エリアなし」。
    CommercialAreaManager old = m;
    old.ResetToDefault();
    CHECK(old.Areas().empty() && old.Withdrawn().empty());
    Json empty = Json::MakeObject();
    CommercialAreaManager fromEmpty = m;
    LoadContext lc;
    fromEmpty.Load(empty, 1, lc);
    CHECK(fromEmpty.Areas().empty() && fromEmpty.PlaceArea(1.0f, 1.0f, cell) == 1);

    // 壊れた項目は、読み飛ばす(IDが0・重複・面積なし・重なる・範囲外の列挙)。他の区画は、読める。
    Json bad = Json::MakeObject();
    Json areas = Json::MakeArray();
    auto item = [&](int id, float x0, float z0, float x1, float z1, int achieved)
    {
        Json j = Json::MakeObject();
        j.Set("id", id); j.Set("minX", x0); j.Set("minZ", z0); j.Set("maxX", x1); j.Set("maxZ", z1); j.Set("achieved", achieved);
        areas.Push(std::move(j));
    };
    item(0, 0, 0, 8, 8, 0);          // ID 0
    item(5, 0, 0, 8, 8, 99);         // 正しい(列挙が範囲外 → 既定)
    item(5, 20, 0, 28, 8, 0);        // IDの重複
    item(6, 4, 0, 12, 8, 0);         // 5 と重なる
    item(7, 10, 10, 10, 18, 0);      // 面積なし
    item(8, 8, 0, 16, 8, 3);         // 隣接(OK)
    bad.Set("areas", std::move(areas));
    bad.Set("nextId", 2);            // 小さすぎる値でも、読んだIDより大きくなる
    CommercialAreaManager loadedBad;
    loadedBad.Load(bad, 1, lc);
    CHECK(loadedBad.Areas().size() == 2 && loadedBad.Areas()[0].id == 5 && loadedBad.Areas()[1].id == 8);
    CHECK(loadedBad.Areas()[0].achieved == AreaMilestone::None && loadedBad.Areas()[1].achieved == AreaMilestone::Complete);
    CHECK(loadedBad.PlaceArea(100.0f, 100.0f, cell) == 9);
    CHECK(loadedBad.Validate().empty());
}

// ---- P2-B: 施設構成の評価(供給量と割合の区別・用途の充実度・単純な専門性) ----
static AreaComposition Comp(std::initializer_list<BuildingType> types, const CommercialAreaSettings& s = CommercialAreaSettings())
{
    return ComputeAreaComposition(std::vector<BuildingType>(types), s);
}

static void TestComposition()
{
    const BuildingType Shop = BuildingType::Shop, Super = BuildingType::Supermarket, Cinema = BuildingType::Cinema, Cafe = BuildingType::Cafe;
    const int TopUp = static_cast<int>(UsagePurpose::TopUp), Bulk = static_cast<int>(UsagePurpose::BulkBuy);
    const int Movie = static_cast<int>(UsagePurpose::Movie), Light = static_cast<int>(UsagePurpose::Light);

    // 店なし: 0。壊れない(NaNを作らない)。
    const AreaComposition none = Comp({});
    CHECK(none.storeCount == 0 && none.character == AreaCharacter::None && none.coveredPurposes == 0 && none.overlapStores == 0);
    CHECK(none.dailyShare == 0.0f && none.specialization == 0.0f);

    // 供給量は、元の適性値の合計(正規化していない)。コンビニ1軒 = {1.6, 0.5, 0.1, 0.8}。
    const AreaComposition oneShop = Comp({ Shop });
    CHECK(Near(oneShop.supply[TopUp], 1.6f) && Near(oneShop.supply[Bulk], 0.5f) && Near(oneShop.supply[Movie], 0.1f) && Near(oneShop.supply[Light], 0.8f));
    // 割合は、供給量を合計(3.0)で割った分布。元の値とは別。
    CHECK(Near(oneShop.share[TopUp], 1.6f / 3.0f) && Near(oneShop.share[Bulk], 0.5f / 3.0f));
    float shareSum = 0.0f; for (float v : oneShop.share) { shareSum += v; }
    CHECK(Near(shareSum, 1.0f));

    // 同じ構成の比率なら、割合は同じ。店の数(供給量・競合の重なり)は、別に見える(要望①)。
    const AreaComposition two = Comp({ Shop, Cafe });
    const AreaComposition six = Comp({ Shop, Cafe, Shop, Cafe, Shop, Cafe });
    for (int p = 0; p < kUsagePurposeCount; ++p) { CHECK(Near(two.share[p], six.share[p])); }
    CHECK(Near(two.specialization, six.specialization) && two.character == six.character);
    CHECK(two.storeCount == 2 && six.storeCount == 6);
    CHECK(Near(six.supply[TopUp], two.supply[TopUp] * 3.0f) && Near(six.supply[Light], two.supply[Light] * 3.0f));
    CHECK(two.overlapStores == 0 && six.overlapStores == 4);                // 強い店: コンビニ3(買い足し)・カフェ3(軽食) → 各2重なり
    CHECK(two.strongCount[TopUp] == 1 && six.strongCount[TopUp] == 3 && six.strongCount[Light] == 3);
    CHECK(two.coveredPurposes == 2 && six.coveredPurposes == 2);            // 用途の充実度は、店の数では増えない

    // 用途の充実度(強い適性を持つ店がある用途の種類数)。
    CHECK(Comp({ Shop }).coveredPurposes == 1);
    CHECK(Comp({ Shop, Super }).coveredPurposes == 2);
    CHECK(Comp({ Cinema, Cafe }).coveredPurposes == 2);
    CHECK(Comp({ Shop, Super, Cinema, Cafe }).coveredPurposes == 4);
    CHECK(Comp({ Shop, Shop, Shop }).coveredPurposes == 1 && Comp({ Shop, Shop, Shop }).overlapStores == 2);

    // 特色のラベル(日用品寄りの割合): 日用品の店だけ=特化、娯楽の店だけ=特化、両方=混合。
    CHECK(Comp({ Shop, Super }).character == AreaCharacter::DailyFocused);
    CHECK(Comp({ Cinema, Cafe }).character == AreaCharacter::LeisureFocused);
    CHECK(Comp({ Shop, Super, Cinema, Cafe }).character == AreaCharacter::Mixed);
    CHECK(Comp({ Shop, Super }).dailyShare > 0.7f && Comp({ Cinema, Cafe }).dailyShare < 0.3f);

    // 専門性: 単純な式(一番大きい割合と、均等の差)。1つの用途に偏るほど大きい。0〜1に収まる。
    const AreaComposition allFour = Comp({ Shop, Super, Cinema, Cafe });
    const AreaComposition shops3 = Comp({ Shop, Shop, Shop });
    CHECK(shops3.specialization > allFour.specialization);
    for (const AreaComposition& c : { oneShop, two, six, allFour, shops3 }) { CHECK(c.specialization >= 0.0f && c.specialization <= 1.0f); }
    {
        const float maxShare = (std::max)((std::max)(allFour.share[0], allFour.share[1]), (std::max)(allFour.share[2], allFour.share[3]));
        CHECK(Near(allFour.specialization, (maxShare - 0.25f) / 0.75f));   // 式のとおり
    }

    // 専門型・汎用型のどちらかが、有利にならない: 評価は、集客・売上・品質に、何も掛からない(値を返すだけ)。
    // (施設の品質・値段は、変わらない)
    CHECK(Near(FindFacilityDefinition(Shop)->quality, 1.0f) && FindFacilityDefinition(Shop)->pricePerVisit == 120);

    // 「強い適性」のしきい値は、元の適性値と比べる(設定)。上げると、強い店が減る。
    CommercialAreaSettings strict;
    strict.strongSuitability = 1.7f;                                         // コンビニ 1.6・スーパー 1.6 は強くない。映画館 2.0・カフェ 1.8 は強い
    CHECK(Comp({ Shop, Super, Cinema, Cafe }, strict).coveredPurposes == 2);
    CommercialAreaSettings loose;
    loose.strongSuitability = 0.5f;                                          // コンビニの まとめ買い 0.5・軽食 0.8 も、強い
    CHECK(Comp({ Shop }, loose).coveredPurposes == 3 && Comp({ Shop }, loose).strongCount[Bulk] == 1);
    // ラベルのしきい値も設定。
    CommercialAreaSettings narrow;
    narrow.dailyFocusedShare = 0.99f;
    CHECK(Comp({ Shop, Super }, narrow).character == AreaCharacter::Mixed);

    // 住宅・オフィスは、無視する。
    CHECK(Comp({ BuildingType::House, BuildingType::Office, Shop }).storeCount == 1);

    // 管理側: 所属する施設の種類から、区画ごとに評価される。区画が無い・店が無ければ「店なし」。
    CommercialAreaManager m;
    const uint32_t a = m.PlaceArea(4.0f, 4.0f, WorldScale::kCellSize);
    const AreaRect rect = m.Find(a)->rect;
    std::vector<AreaFacility> facilities;
    facilities.push_back(Fac(1, Cinema, Axis(rect.minX + 1.0f, rect.minZ + 1.0f, 1.5f, 1.5f)));
    facilities.push_back(Fac(2, Cafe, Axis(rect.minX + 3.0f, rect.minZ + 1.0f, 1.5f, 1.5f)));
    facilities.push_back(Fac(3, Shop, Axis(rect.maxX + 5.0f, rect.minZ + 1.0f, 1.5f, 1.5f)));   // 区画の外
    m.Refresh(facilities);
    const AreaComposition inArea = m.CompositionOf(a);
    CHECK(inArea.storeCount == 2 && inArea.character == AreaCharacter::LeisureFocused && inArea.coveredPurposes == 2);
    CHECK(m.CompositionOf(999).character == AreaCharacter::None);
    m.DeleteArea(a, 1);
    CHECK(m.CompositionOf(a).storeCount == 0);
    CHECK(AreaCharacterName(AreaCharacter::Mixed) != nullptr);
}

int main()
{
    TestOverlapArea();
    TestMembership();
    TestNoDoubleMembership();
    TestProgress();
    TestOperations();
    TestRefreshAndAchievement();
    TestSaveLoad();
    TestComposition();
    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

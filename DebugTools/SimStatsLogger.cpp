#include "SimStatsLogger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include "CommercialAreaManager.h"
#include "DemandSystem.h"
#include "GameClock.h"
#include "BuildingType.h"
#include "FacilityData.h"

namespace
{
    // CSV・要約の、異常・不成立の理由の名前(英語のキー。表計算ソフトで扱いやすい)。
    const char* ReasonKey(size_t index)
    {
        switch (static_cast<SimReason>(index))
        {
        case SimReason::None:                    return "none";
        case SimReason::OutingNeedDriven:        return "outing_need_driven";
        case SimReason::OutingSpontaneous:       return "outing_spontaneous";
        case SimReason::HomeNoRoad:              return "home_no_road";
        case SimReason::NoFacilityInCategory:    return "no_facility_in_category";
        case SimReason::NoPickableCandidate:     return "no_pickable_candidate";
        case SimReason::StopNoHome:              return "stop_no_home";
        case SimReason::StopMaxReached:          return "stop_max_reached";
        case SimReason::StopTooLate:             return "stop_too_late";
        case SimReason::StopLowStamina:          return "stop_low_stamina";
        case SimReason::StopRollHit:             return "stop_roll_hit";
        case SimReason::StopRollMiss:            return "stop_roll_miss";
        case SimReason::StopNoRoad:              return "stop_no_road";
        case SimReason::StopHomeMissing:         return "stop_home_missing";
        case SimReason::StopNoCandidates:        return "stop_no_candidates";
        case SimReason::StopNoPickable:          return "stop_no_pickable";
        case SimReason::StopFoodChain:           return "stop_food_chain";
        case SimReason::StopStarted:             return "stop_started";
        case SimReason::FacilityDemolished:      return "facility_demolished";
        case SimReason::RoadCut:                 return "road_cut";
        case SimReason::NoRouteHome:             return "no_route_home";
        case SimReason::RoadReconnected:         return "road_reconnected";
        case SimReason::DetourReturn:            return "detour_return";
        case SimReason::FacilityMissingOnArrival:return "facility_missing_on_arrival";
        case SimReason::TestApplied:             return "test_applied";
        case SimReason::TestRejected:            return "test_rejected";
        case SimReason::NoOutingReason:          return "no_outing_reason";
        default:                                 return "unknown";
        }
    }

    const char* AnomalyKey(size_t index)
    {
        switch (static_cast<SimAnomaly>(index))
        {
        case SimAnomaly::NonFiniteValue:            return "NonFiniteValue";
        case SimAnomaly::StaminaOutOfRange:         return "StaminaOutOfRange";
        case SimAnomaly::NeedOutOfRange:            return "NeedOutOfRange";
        case SimAnomaly::SatisfactionOutOfRange:    return "SatisfactionOutOfRange";
        case SimAnomaly::PenaltyOutOfRange:         return "PenaltyOutOfRange";
        case SimAnomaly::StopCountNegative:         return "StopCountNegative";
        case SimAnomaly::WaypointIndexOutOfRange:   return "WaypointIndexOutOfRange";
        case SimAnomaly::MissingTargetFacility:     return "MissingTargetFacility";
        case SimAnomaly::MissingHome:               return "MissingHome";
        case SimAnomaly::HomelessWithHome:          return "HomelessWithHome";
        case SimAnomaly::StopCountExceeded:         return "StopCountExceeded";
        case SimAnomaly::ArrivedWithoutMove:        return "ArrivedWithoutMove";
        case SimAnomaly::SettleWithoutArrival:      return "SettleWithoutArrival";
        case SimAnomaly::DuplicateFacilityInOuting: return "DuplicateFacilityInOuting";
        case SimAnomaly::MoveWhileVisiting:         return "MoveWhileVisiting";
        case SimAnomaly::ReturnWhileVisiting:       return "ReturnWhileVisiting";
        case SimAnomaly::NegativeRevenue:           return "NegativeRevenue";
        case SimAnomaly::SettledOnMissingFacility:  return "SettledOnMissingFacility";
        default:                                    return "Unknown";
        }
    }

    // UTF-8のBOM(表計算ソフトが、日本語を正しく読むため)。
    void WriteBom(std::ofstream& out)
    {
        out << "\xEF\xBB\xBF";
    }

    std::string Now()
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
        return text;
    }

    double Percent(uint64_t part, uint64_t whole)
    {
        return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
    }

    uint64_t Sub(uint64_t a, uint64_t b) { return a >= b ? a - b : 0; }
}

void SimStatsLogger::Initialize(DemandSystem* demand, CommercialAreaManager* areas, GameClock* clock)
{
    m_demand = demand;
    m_areas = areas;
    m_clock = clock;
    if (m_demand)
    {
        m_demand->GetDebugRecorder().AddObserver(this);
    }
}

double SimStatsLogger::GetWallSeconds() const
{
    if (!m_active) { return 0.0; }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
}

void SimStatsLogger::OnEvent(const SimEvent& event)
{
    if (!m_active || event.kind != SimEventKind::Anomaly)
    {
        return;
    }
    ++m_anomalyTotal;
    const size_t index = static_cast<size_t>(event.anomaly);
    if (index < m_anomalyByKind.size())
    {
        ++m_anomalyByKind[index];
    }
}

void SimStatsLogger::OnWorldReset()
{
    m_worldReset = true;
}

bool SimStatsLogger::Start()
{
    if (!m_demand || !m_clock)
    {
        m_message = "まだ準備ができていません";
        return false;
    }
    if (m_active)
    {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(kOutputDirectory, ec);
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    m_folder = std::string(kOutputDirectory) + "/session_" + stamp;
    std::filesystem::create_directories(m_folder, ec);
    if (ec)
    {
        m_message = "フォルダを作れません: " + m_folder;
        return false;
    }
    if (!OpenFolder())
    {
        return false;
    }
    m_active = true;
    m_worldReset = false;
    m_rows = 0;
    m_lastWeekLogged = 0;
    m_weeksEndedSeen = m_demand->GetWeeksEnded();
    m_prev = m_demand->GetBehaviorStats().Scope(SimStatScope::Normal).total;
    m_anomalyTotal = 0;
    m_anomalyByKind.fill(0);
    m_startTime = std::chrono::steady_clock::now();
    Note("記録を開始(第" + std::to_string(m_clock->GetWeekCount()) + "週、週末の回数 " + std::to_string(m_weeksEndedSeen) + ")");
    m_message = "記録中: " + m_folder;
    WriteSummary();
    return true;
}

void SimStatsLogger::Stop()
{
    if (!m_active)
    {
        return;
    }
    WriteSummary();
    Note("記録を終了(行数 " + std::to_string(m_rows) + ")");
    m_active = false;
    m_message = "記録を終了しました: " + m_folder;
}

bool SimStatsLogger::OpenFolder()
{
    {
        std::ofstream out(m_folder + "/weekly.csv");
        if (!out) { m_message = "ファイルを作れません: " + m_folder; return false; }
        WriteBom(out);
        out << "week_ended,game_week,wall_minutes,time_scale,weeks_covered,residents,at_home,outing,stranded,homeless,"
               "avg_stamina,avg_life_need,avg_activity_need,avg_satisfaction,life_unmet,activity_unmet,"
               "outings,reason_quick,reason_substantial,reason_area,outing_failed,departed,main_visits,returned_home,"
               "stop_considered,stop_started,stop_visits,visits,revenue,stamina_used,"
               "area_outings,area_visitors,area_purpose_visitors,area_visits,area_sales,"
               "standalone_visits,standalone_sales,facility_visits,facility_sales,area_visit_share,anomalies\n";
    }
    {
        std::ofstream out(m_folder + "/facilities.csv");
        if (!out) { m_message = "ファイルを作れません: " + m_folder; return false; }
        WriteBom(out);
        out << "week_ended,facility_id,type,area_id,visits_last_week,sales_last_week,visits_total,sales_total,demolished\n";
    }
    {
        std::ofstream out(m_folder + "/areas.csv");
        if (!out) { m_message = "ファイルを作れません: " + m_folder; return false; }
        WriteBom(out);
        out << "week_ended,area_id,published,members,store_count,covered_purposes,"
               "outings,visitors,purpose_visitors,not_used,visits,sales,"
               "total_outings,total_visitors,total_purpose_visitors,total_visits,total_sales\n";
    }
    {
        std::ofstream out(m_folder + "/notes.txt");
        if (!out) { m_message = "ファイルを作れません: " + m_folder; return false; }
        WriteBom(out);
    }
    return true;
}

void SimStatsLogger::Note(const std::string& text)
{
    if (m_folder.empty()) { return; }
    std::ofstream out(m_folder + "/notes.txt", std::ios::app);
    if (out) { out << Now() << "  " << text << "\n"; }
}

void SimStatsLogger::Update(bool canRun)
{
    if (!m_demand || !m_clock || !canRun)
    {
        return;
    }
    if (!m_active)
    {
        if (autoStart && !m_autoTried)
        {
            m_autoTried = true;
            Start();
        }
        return;
    }

    const int weeksEnded = m_demand->GetWeeksEnded();
    // 世界が差し替わった(読み込み・巻き戻し・新しい街)。通算の集計も消えているので、基準を取り直す。
    if (m_worldReset || weeksEnded < m_weeksEndedSeen)
    {
        m_worldReset = false;
        m_weeksEndedSeen = weeksEnded;
        m_prev = m_demand->GetBehaviorStats().Scope(SimStatScope::Normal).total;
        Note("世界が差し替わったので、基準を取り直した(週末の回数 " + std::to_string(weeksEnded) + ")");
        return;
    }
    if (weeksEnded > m_weeksEndedSeen)
    {
        WriteWeekRows();
        m_weeksEndedSeen = weeksEnded;
        WriteSummary();
    }
}

void SimStatsLogger::WriteWeekRows()
{
    const SimStats& stats = m_demand->GetBehaviorStats();
    const SimStatBucket& cur = stats.Scope(SimStatScope::Normal).total;
    const SimStatBucket& prev = m_prev;
    const int weeksEnded = m_demand->GetWeeksEnded();
    const int covered = weeksEnded - m_weeksEndedSeen;
    const SimPopulationSnapshot pop = m_demand->GetPopulationSnapshot();

    // 施設・区画の台帳(直前の週)。施設は、今の所属する区画で、区画の中か外かに分ける。
    long long areaVisits = 0, areaSales = 0, standaloneVisits = 0, standaloneSales = 0;
    std::ofstream fac(m_folder + "/facilities.csv", std::ios::app);
    for (const FacilityRecord& rec : m_demand->GetLedger().GetRecords())
    {
        const uint32_t areaId = m_demand->GetAreaOfFacility(rec.objectId);
        if (areaId != 0) { areaVisits += rec.lastWeek.visits; areaSales += rec.lastWeek.sales; }
        else { standaloneVisits += rec.lastWeek.visits; standaloneSales += rec.lastWeek.sales; }
        if (fac)
        {
            fac << weeksEnded << "," << rec.objectId << "," << GetBuildingDefinition(rec.type).name << "," << areaId << ","
                << rec.lastWeek.visits << "," << rec.lastWeek.sales << "," << rec.total.visits << "," << rec.total.sales << ","
                << (rec.demolished ? 1 : 0) << "\n";
        }
    }

    long long areaOutings = 0, areaVisitors = 0, areaPurposeVisitors = 0, areaLedgerVisits = 0, areaLedgerSales = 0;
    {
        std::ofstream ar(m_folder + "/areas.csv", std::ios::app);
        for (const AreaRecord& rec : m_demand->GetAreaLedger().GetRecords())
        {
            const CommercialArea* area = m_areas ? m_areas->Find(rec.areaId) : nullptr;
            const AreaMembers* members = m_areas ? m_areas->MembersOf(rec.areaId) : nullptr;
            const AreaComposition composition = (m_areas && area) ? m_areas->CompositionOf(rec.areaId) : AreaComposition();
            areaOutings += rec.lastWeek.areaOutings;
            areaVisitors += rec.lastWeek.visitors;
            areaPurposeVisitors += rec.lastWeek.purposeVisitors;
            areaLedgerVisits += rec.lastWeek.visits;
            areaLedgerSales += rec.lastWeek.sales;
            if (ar)
            {
                ar << weeksEnded << "," << rec.areaId << "," << (area ? (area->published ? 1 : 0) : -1) << ","
                   << (members ? members->facilityIds.size() : 0) << "," << composition.storeCount << "," << composition.coveredPurposes << ","
                   << rec.lastWeek.areaOutings << "," << rec.lastWeek.visitors << "," << rec.lastWeek.purposeVisitors << ","
                   << (std::max)(0, rec.lastWeek.areaOutings - rec.lastWeek.purposeVisitors) << ","
                   << rec.lastWeek.visits << "," << rec.lastWeek.sales << ","
                   << rec.total.areaOutings << "," << rec.total.visitors << "," << rec.total.purposeVisitors << "," << rec.total.visits << "," << rec.total.sales << "\n";
            }
        }
    }
    (void)areaLedgerVisits; (void)areaLedgerSales;

    const long long facilityVisits = areaVisits + standaloneVisits;
    const long long facilitySales = areaSales + standaloneSales;
    const double wallMinutes = GetWallSeconds() / 60.0;

    std::ofstream out(m_folder + "/weekly.csv", std::ios::app);
    if (!out)
    {
        m_message = "weekly.csv に書けません";
        return;
    }
    char avg[160];
    std::snprintf(avg, sizeof(avg), "%.2f,%.2f,%.2f,%.2f", pop.avgStamina, pop.avgLifeNeed, pop.avgActivityNeed, pop.avgSatisfaction);
    char wall[32];
    std::snprintf(wall, sizeof(wall), "%.2f", wallMinutes);
    char revenue[32];
    std::snprintf(revenue, sizeof(revenue), "%.0f", cur.revenue - prev.revenue);
    char stamina[32];
    std::snprintf(stamina, sizeof(stamina), "%.1f", cur.staminaUsed - prev.staminaUsed);
    char share[32];
    std::snprintf(share, sizeof(share), "%.3f", facilityVisits > 0 ? static_cast<double>(areaVisits) / static_cast<double>(facilityVisits) : 0.0);

    out << weeksEnded << "," << m_clock->GetWeekCount() << "," << wall << "," << m_clock->GetTimeScale() << "," << covered << ","
        << pop.residents << "," << pop.atHome << "," << pop.outing << "," << pop.stranded << "," << pop.homeless << ","
        << avg << "," << pop.lifeUnmet << "," << pop.activityUnmet << ","
        << Sub(cur.outingStarted, prev.outingStarted) << ","
        << Sub(cur.outingReasonChosen[0], prev.outingReasonChosen[0]) << ","
        << Sub(cur.outingReasonChosen[1], prev.outingReasonChosen[1]) << ","
        << Sub(cur.outingReasonChosen[2], prev.outingReasonChosen[2]) << ","
        << Sub(cur.outingFailed, prev.outingFailed) << "," << Sub(cur.departed, prev.departed) << ","
        << Sub(cur.mainVisits, prev.mainVisits) << "," << Sub(cur.returnedHome, prev.returnedHome) << ","
        << Sub(cur.stopConsidered, prev.stopConsidered) << "," << Sub(cur.stopStarted, prev.stopStarted) << ","
        << Sub(cur.stopVisits, prev.stopVisits) << "," << Sub(cur.visits, prev.visits) << ","
        << revenue << "," << stamina << ","
        << areaOutings << "," << areaVisitors << "," << areaPurposeVisitors << "," << areaVisits << "," << areaSales << ","
        << standaloneVisits << "," << standaloneSales << "," << facilityVisits << "," << facilitySales << ","
        << share << "," << m_anomalyTotal << "\n";

    m_prev = cur;
    ++m_rows;
    m_lastWeekLogged = weeksEnded;
}

void SimStatsLogger::WriteSummary()
{
    if (!m_demand || m_folder.empty())
    {
        return;
    }
    std::ofstream out(m_folder + "/summary.txt");
    if (!out)
    {
        return;
    }
    WriteBom(out);
    const SimStats& stats = m_demand->GetBehaviorStats();
    const SimStatsScopeData& scope = stats.Scope(SimStatScope::Normal);
    // 記録を始めてからの差ではなく、集計を始めてからの通算(世界を読み込み直すと、リセットされる)。
    const SimStatBucket& b = scope.total;
    const SimPopulationSnapshot pop = m_demand->GetPopulationSnapshot();

    out << "行動統計の要約(週が区切られるたびに、書き直す)\n";
    out << "更新: " << Now() << "\n";
    out << "記録の開始からの経過: " << static_cast<int>(GetWallSeconds() / 60.0) << " 分 / 記録した週: " << m_rows
        << " 行(最後の週末の回数 " << m_lastWeekLogged << ") / 時計の倍率 " << (m_clock ? m_clock->GetTimeScale() : 0.0f) << "\n";
    out << "統計を数え始めた週: " << stats.StartWeek() << " / 今の週: " << stats.CurrentWeek() << "\n";
    out << "注意: 世界の読み込み・巻き戻しで、通算の集計はリセットされる(notes.txt に印を残す)。\n\n";

    out << "== 今の住民 ==\n";
    out << "住民 " << pop.residents << " 人(家 " << pop.atHome << " / 外出 " << pop.outing << " / 立ち往生 " << pop.stranded << " / 住居なし " << pop.homeless << ")\n";
    out << "平均 体力 " << pop.avgStamina << " / 生活の需要 " << pop.avgLifeNeed << " / 余暇の需要 " << pop.avgActivityNeed << " / 満足度 " << pop.avgSatisfaction
        << "(不足している人: 生活 " << pop.lifeUnmet << ", 余暇 " << pop.activityUnmet << ")\n\n";

    out << "== 外出(通算) ==\n";
    out << "外出を考えた " << b.outingStarted << " / 目的を決めた " << b.purposeChosen << " / 出発した " << b.departed << " / 出られなかった " << b.outingFailed
        << " / 帰宅 " << b.returnedHome << " / 立ち往生 " << b.stranded << " / 中断 " << b.aborted << "\n";
    char line[256];
    {
        const uint64_t chosen = b.outingReasonChosen[0] + b.outingReasonChosen[1] + b.outingReasonChosen[2];
        std::snprintf(line, sizeof(line), "外出の理由: 手軽 %llu (%.1f%%) / がっつり %llu (%.1f%%) / エリア %llu (%.1f%%)\n",
                      (unsigned long long)b.outingReasonChosen[0], Percent(b.outingReasonChosen[0], chosen),
                      (unsigned long long)b.outingReasonChosen[1], Percent(b.outingReasonChosen[1], chosen),
                      (unsigned long long)b.outingReasonChosen[2], Percent(b.outingReasonChosen[2], chosen));
        out << line;
    }
    for (size_t i = 0; i < b.outingFailReason.size(); ++i)
    {
        if (b.outingFailReason[i] > 0)
        {
            out << "  出られなかった理由 " << ReasonKey(i) << ": " << b.outingFailReason[i] << "\n";
        }
    }
    out << "\n== 立ち寄り・利用(通算) ==\n";
    out << "立ち寄りを考えた " << b.stopConsidered << " / 寄った " << b.stopStarted << " (" ;
    std::snprintf(line, sizeof(line), "%.1f%%", Percent(b.stopStarted, b.stopConsidered));
    out << line << ") / 立ち寄り先の利用 " << b.stopVisits << "\n";
    for (size_t i = 0; i < b.stopReason.size(); ++i)
    {
        if (b.stopReason[i] > 0)
        {
            std::snprintf(line, sizeof(line), "  %-28s %llu (%.1f%%)\n", ReasonKey(i), (unsigned long long)b.stopReason[i], Percent(b.stopReason[i], b.stopConsidered));
            out << line;
        }
    }
    out << "利用 " << b.visits << " 回(主目的 " << b.mainVisits << " + 立ち寄り " << b.stopVisits << ") / 売上 " << static_cast<long long>(b.revenue)
        << " 円 / 体力の消費 " << static_cast<long long>(b.staminaUsed) << " / 生活需要の回復 " << static_cast<long long>(b.lifeRecovered) << "\n";
    if (b.outingStarted > 0)
    {
        std::snprintf(line, sizeof(line), "1回の外出あたり: 利用 %.2f 回 / 売上 %.0f 円\n", static_cast<double>(b.visits) / static_cast<double>(b.outingStarted), b.revenue / static_cast<double>(b.outingStarted));
        out << line;
    }

    out << "\n== 確率の判定(期待した当選の数と、実際の数。差が標準偏差の3倍を超えたら、偏りを疑う) ==\n";
    auto roll = [&](const char* name, const SimRollTally& t)
    {
        if (t.trials == 0) { return; }
        const double sd = t.StdDev();
        const double diff = static_cast<double>(t.hits) - t.ExpectedHits();
        std::snprintf(line, sizeof(line), "  %-22s 判定 %llu / 当選 %llu / 期待 %.1f / 差 %+.1f (標準偏差 %.1f%s)\n", name,
                      (unsigned long long)t.trials, (unsigned long long)t.hits, t.ExpectedHits(), diff, sd,
                      (sd > 0.0 && std::fabs(diff) > 3.0 * sd) ? " ※偏り" : "");
        out << line;
    };
    roll("外出判定(需要)", b.outingRollNeed);
    roll("外出判定(自発)", b.outingRollSpontaneous);
    roll("立ち寄りの確率", b.stopRoll);

    out << "\n== 施設(通算の台帳。所属する区画つき) ==\n";
    {
        long long areaV = 0, areaS = 0, standV = 0, standS = 0;
        std::vector<const FacilityRecord*> records;
        for (const FacilityRecord& rec : m_demand->GetLedger().GetRecords()) { records.push_back(&rec); }
        std::sort(records.begin(), records.end(), [](const FacilityRecord* a, const FacilityRecord* c) { return a->total.visits > c->total.visits; });
        for (const FacilityRecord* rec : records)
        {
            const uint32_t areaId = m_demand->GetAreaOfFacility(rec->objectId);
            std::snprintf(line, sizeof(line), "  #%-4u %-12s 区画 %-3u 来訪 %-6d 売上 %-8d%s\n", rec->objectId, GetBuildingDefinition(rec->type).name, areaId,
                          rec->total.visits, rec->total.sales, rec->demolished ? " (解体済み)" : "");
            out << line;
            if (areaId != 0) { areaV += rec->total.visits; areaS += rec->total.sales; } else { standV += rec->total.visits; standS += rec->total.sales; }
        }
        out << "区画の中の施設: 来訪 " << areaV << " / 売上 " << areaS << "   区画の外の単独の施設: 来訪 " << standV << " / 売上 " << standS << "\n";
    }
    out << "\n== 商業エリア(通算) ==\n";
    for (const AreaRecord& rec : m_demand->GetAreaLedger().GetRecords())
    {
        const CommercialArea* area = m_areas ? m_areas->Find(rec.areaId) : nullptr;
        std::snprintf(line, sizeof(line), "  区画 #%u (%s): 目的にした人数 %d / 来訪者 %d / 目的の来訪者 %d / 向かったが使っていない %d / 延べ利用 %d / 売上 %d\n", rec.areaId,
                      area ? (area->published ? "周知済み" : "未周知") : "削除済み",
                      rec.total.areaOutings, rec.total.visitors, rec.total.purposeVisitors,
                      (std::max)(0, rec.total.areaOutings - rec.total.purposeVisitors), rec.total.visits, rec.total.sales);
        out << line;
    }

    out << "\n== 住民ごとのばらつき(通算の利用回数) ==\n";
    {
        std::vector<uint64_t> visits;
        for (const auto& pair : scope.residents) { visits.push_back(pair.second.visits); }
        if (visits.empty())
        {
            out << "  まだ記録がありません\n";
        }
        else
        {
            std::sort(visits.begin(), visits.end());
            size_t zero = 0;
            for (uint64_t v : visits) { if (v == 0) { ++zero; } }
            out << "  人数 " << visits.size() << " / 最小 " << visits.front() << " / 中央値 " << visits[visits.size() / 2] << " / 最大 " << visits.back()
                << " / 一度も使っていない人 " << zero << "\n";
        }
    }

    out << "\n== 異常(記録中) ==\n";
    out << "合計 " << m_anomalyTotal << "\n";
    for (size_t i = 1; i < m_anomalyByKind.size(); ++i)
    {
        if (m_anomalyByKind[i] > 0) { out << "  " << AnomalyKey(i) << ": " << m_anomalyByKind[i] << "\n"; }
    }
}

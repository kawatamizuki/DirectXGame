#pragma once
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>
#include "ResidentAgent.h"
#include "ResidentLogic.h"
#include "ResidentSimSettings.h"

// 住民シミュレーションの「意思決定・分岐・状態遷移・資源変化・異常」を、上限付きで記録する、デバッグ専用のデータ収集部。
//
//  ・DemandSystem は、記録の窓口(このクラス)に値を渡すだけ。ImGui・画面には依存しない(表示は DebugTools/ResidentDebugPanel)。
//  ・記録は「実際の処理で使った値」をそのまま受け取る。ここで乱数を引いたり、結果を推測したりしない。
//    記録のON/OFFで、シミュレーションの乱数の消費順・結果は変わらない(OFFなら、ほぼ何もしない)。
//  ・関数の呼び出しを全て記録するのではなく、意味のあるイベントだけ。人間向けの説明文は、UI側で作る(ここは数値と列挙型だけ)。
//  ・容量に上限がある(リングバッファ)。上限で古いものを捨てたら、その件数を覚えて、UIに出せるようにする。
//  ・Releaseのビルドでは、有効にしない(DemandSystem が既定で無効のまま持つだけ)。

// ---------------------------------------------------------------------------
// イベント
// ---------------------------------------------------------------------------

// イベントの種類。値は、後から足すだけにする(並びを変えない)。a/b/cの意味は、種類ごとに下に書いてある。
enum class SimEventKind : uint8_t
{
    OutingRoll,         // 外出判定に当選した(自宅で、外出を考え始めた)。a=外出の確率 b=引いた乱数。※不発(外れ)は、イベントにせず、住民ごとの集計に数える
    PurposeChoice,      // 外出の目的(カテゴリ・利用目的)を決めた。detailIdに、乱数などの内訳
    FacilitySelect,     // 施設を重み付き抽選で選んだ(選べなかった場合も)。detailIdに、候補ごとの内訳。facilityIdは選ばれた施設
    OutingBlocked,      // 外出を考えたが、出られなかった(自宅の近くに道路が無い・目的の施設が無い)。reasonに理由
    MoveStart,          // 施設へ歩き始めた。a=経路の長さ(ワールド単位) facilityId=行き先
    Arrived,            // 施設に着いて、利用を始めた。a=滞在予定(ゲーム時間) facilityId=施設
    VisitFinished,      // 施設の利用が終わった(回復・売上・体力の消費を反映した後)。a=売上(円) b=生活需要の回復量 c=体力の消費量
    StopoverCheck,      // 立ち寄りを考えた結果(寄らなかった場合も)。reasonが理由。a=寄る確率 b=引いた乱数(引いた時だけ)
    ReturnStart,        // 帰宅を始めた。a=経路の長さ
    ReturnRerouted,     // 道路が無くなって、迂回して帰ることになった。a=迂回で増えた道のり b=体力の消費
    ReturnHome,         // 家に着いた(外出が終わった)
    OutingAborted,      // 行き先の施設が解体されて、外出が中断された。facilityIdは無くなった施設
    BecameStranded,     // 帰れなくなった(立ち往生)。reasonが理由
    StrandedRecovered,  // 道がつながって、帰り始めた
    BecameHomeless,     // 帰る家が無くなった(住居なし)
    HomeDemolished,     // 住んでいた住宅が解体された
    Anomaly,            // 異常を検出した(不変条件の違反)。anomalyに種類。a=問題の値
    MovedIn             // 住居なしの住民が、新しい住宅(facilityId)への入居を決めた(プレイヤーが選んだ)
};

// 結果の区別(表示の色分けに使う)。普通の不成立と、本当の異常を混ぜない。
enum class SimEventResult : uint8_t
{
    Info,       // 経過の記録(結果の成否はない)
    Success,    // 成功
    NotMet,     // 不成立(確率・条件による、普通の結果。異常ではない)
    Warning,    // 注意(普通ではないが、設計上ありえる)
    Violation   // 確認された不変条件違反(異常)
};

// 理由・種別。成否の「なぜ」。値は、後から足すだけにする。
enum class SimReason : uint8_t
{
    None,
    OutingNeedDriven,       // (OutingRoll)需要の不足による外出の判定だった
    OutingSpontaneous,      // (OutingRoll)自発的な外出の判定だった
    HomeNoRoad,             // 自宅の近くに道路が無い
    NoFacilityInCategory,   // 目的のカテゴリの施設が1軒も無い
    NoPickableCandidate,    // 候補はあったが、全て除外された(到達不可・体力不足など)
    StopNoHome,             // (立ち寄り)帰る家が無い
    StopMaxReached,         // (立ち寄り)立ち寄りの回数が上限
    StopTooLate,            // (立ち寄り)時刻が遅い
    StopLowStamina,         // (立ち寄り)体力が足りない
    StopRollHit,            // (立ち寄り)確率の抽選に当選した(この後、候補を調べる)
    StopRollMiss,           // (立ち寄り)確率の抽選に外れた(多くの外出は、ここ)
    StopNoRoad,             // (立ち寄り)今いる施設・家の近くに道路が無い
    StopHomeMissing,        // (立ち寄り)帰る家のオブジェクトが見つからない
    StopNoCandidates,       // (立ち寄り)同じカテゴリの他の施設が無い
    StopNoPickable,         // (立ち寄り)候補は有ったが、全て除外された(範囲外・体力不足・使用済みなど)
    StopFoodChain,          // (立ち寄り)食事系が続くので、寄らないことにした
    StopStarted,            // (立ち寄り)寄ることにした
    FacilityDemolished,     // 行き先・利用中の施設が解体された
    RoadCut,                // 道路が切り取られた
    NoRouteHome,            // 家への経路が無い
    RoadReconnected,        // 道がつながった
    DetourReturn,           // 迂回して帰る
    FacilityMissingOnArrival// 着いたが、施設が無くなっていた
};

// 異常(不変条件の違反)の種類。ビットマスクで持つ(1u << 値)。値は、後から足すだけにする。
enum class SimAnomaly : uint8_t
{
    None,
    NonFiniteValue,         // 体力・需要・満足度・時間などが、NaN/Infになった
    StaminaOutOfRange,      // 体力が、負、または、住民の基準の上限を超えている(ペナルティで上限が下がっても、体力は下がらないので、実効の上限との比較はしない)
    NeedOutOfRange,         // 生活・アクティビティの需要が、0〜100の範囲外
    SatisfactionOutOfRange, // 満足度が、0〜上限の範囲外
    PenaltyOutOfRange,      // ペナルティの段階が、負、または、極端に大きい
    StopCountNegative,      // 今の外出の立ち寄り回数が、負
    WaypointIndexOutOfRange,// 移動中なのに、経路の添字が、経路の外
    MissingTargetFacility,  // 向かっている・利用中なのに、行き先の施設が無い(0、または、オブジェクトが無い)
    MissingHome,            // 家のid(0でない)のオブジェクトが無い、または、家にいるのに家のidが0
    HomelessWithHome        // 住居なしなのに、家のidが0でない
};

// 異常のビット。
constexpr uint32_t SimAnomalyBit(SimAnomaly anomaly) { return 1u << static_cast<uint32_t>(anomaly); }

// 判断した時点の、住民の状態の写し(現在値で上書きしない。過去のイベントを見る時に、その時の値を出せる)。
struct SimResidentSnapshot
{
    ResidentAgentState state = ResidentAgentState::AtHome;
    float stamina = 0.0f;
    float staminaCap = 0.0f;
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    float satisfaction = 0.0f;
    int penaltyLevel = 0;
    int outingStopCount = 0;
    uint32_t homeObjectId = 0;
    uint32_t targetFacilityId = 0;
};

// 記録の1件。人間向けの文章は持たない(数値と列挙型だけ。表示はUIが作る)。
struct SimEvent
{
    uint64_t eventId = 0;           // 通し番号(単調増加。記録を捨てても、番号は戻らない)
    uint32_t runId = 0;             // 何回目の「世界」か(ロード・リセットで増える。古い番号の記録は残らない)
    uint32_t residentId = 0;        // 住民の見た目のオブジェクトのid(ResidentAgent::objectId。この世界の中で、変わらない)
    uint32_t outingId = 0;          // 外出の通し番号(0=外出に属さない、または、記録を始める前から続いていた外出)
    int week = 0;                   // 通算の週
    float hour = 0.0f;              // 時刻(ゲーム内)
    SimEventKind kind = SimEventKind::OutingRoll;
    SimEventResult result = SimEventResult::Info;
    SimReason reason = SimReason::None;
    SimAnomaly anomaly = SimAnomaly::None; // kindがAnomalyの時だけ
    int stopIndex = 0;              // 立ち寄りの何回目か(0=主目的)
    uint32_t facilityId = 0;        // 関連する施設のid(0=なし)
    uint64_t detailId = 0;          // 判断の内訳(SimDecisionDetail)のid(0=なし。容量の上限で捨てられていることもある)
    float a = 0.0f;                 // 種類ごとの数値(SimEventKindのコメント参照)
    float b = 0.0f;
    float c = 0.0f;
    SimResidentSnapshot snapshot;   // 判断した時点の住民の状態
};

// ---------------------------------------------------------------------------
// 判断の内訳
// ---------------------------------------------------------------------------

// 施設選び(外出・立ち寄り)1回ぶんの、判断の内訳。「判断時の値」を丸ごと持つので、後から設定や住民が変わっても、その時の計算を読み返せる。
struct SimDecisionDetail
{
    uint64_t detailId = 0;
    uint32_t residentId = 0;
    uint32_t outingId = 0;
    int week = 0;
    float hour = 0.0f;
    int stopIndex = 0;                          // 立ち寄りの何回目か(0=主目的)
    bool isStopover = false;

    // 立ち寄りの確率の抽選(isStopoverの時だけ)。
    float stopChance = 0.0f;                    // 寄る確率
    float stopRoll = -1.0f;                     // 引いた乱数

    // 判断した時の、設定・住民の特徴・状態。
    ResidentSimSettings settings;
    ResidentTraits traits;
    float stamina = 0.0f;
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    std::vector<VisitRecord> history;           // 判断した時の、利用履歴(新しい順)

    // 目的の決定。
    OutingPurposeChoice purpose;
    PurposeTrace purposeTrace;
    bool needDrivenDaily = false;               // 不足による日用品の外出か(再訪補正の下げ幅が弱まる)

    // 施設の抽選。
    std::vector<CandidateEvaluation> evaluations;
    FacilityChoiceTrace choice;
    int chosenIndex = -1;                       // 最終的に選ばれた候補(食事系で取り消された時は-1)

    // 食事系の連続(立ち寄りで、選ばれた施設が食事系の時だけ)。
    bool foodChainChecked = false;
    float foodChainAcceptChance = 1.0f;         // 実際に寄る確率
    float foodChainRoll = -1.0f;                // 引いた乱数(確率が1未満の時だけ)
    bool foodChainPassed = true;                // 通ったか
};

// ---------------------------------------------------------------------------
// 住民ごとの集計・全体の状態
// ---------------------------------------------------------------------------

// 外出判定の、毎ティックの結果の集計(不発はイベントにしないので、ここに数える)。
struct SimOutingRollSummary
{
    uint32_t checks = 0;            // 判定した回数(記録を始めてから。0なら、まだ判定していない)
    uint32_t hits = 0;              // 当選した回数
    uint32_t consecutiveMisses = 0; // 今の連続の不発の回数(当選したら0)
    OutingChanceBranch lastBranch = OutingChanceBranch::NeedDriven; // 直近の判定が、どの場合か(checksが0より大きい時だけ意味を持つ)
    float lastChance = 0.0f;        // 直近の判定の確率
    float lastRoll = -1.0f;         // 直近の判定の乱数
    bool lastHit = false;
    int lastWeek = 0;
    float lastHour = 0.0f;
};

// 住民ごとの、記録の状態。
struct SimResidentRecord
{
    uint32_t currentOutingId = 0;   // 今の外出の通し番号(外出中でなければ0)
    SimOutingRollSummary roll;      // 外出判定の集計
    uint32_t activeAnomalies = 0;   // 今、違反している異常のビット(同じ異常を、毎回記録しないために覚える)
};

// 直近の需要の更新(ティック)で、新しく外出を考える条件が整っていたか。
struct SimTickGate
{
    bool valid = false;             // 記録があるか
    bool canDepart = false;         // 新しい外出を考えてよかったか
    bool sleeping = false;          // 就寝中だったか
    bool noFacilities = false;      // 施設が1軒も無かったか
    bool tooLate = false;           // 新しく外出しない時刻(lastDepartureHour以降)だったか
    int week = 0;
    float hour = 0.0f;
    uint64_t tickIndex = 0;         // 何回目のティックか(記録を始めてから)
};

// 記録の使用状況(UIに出す)。
struct SimRecorderStats
{
    uint32_t runId = 0;
    uint64_t eventsRecordedTotal = 0;   // この世界で、記録したイベントの総数
    uint64_t eventsDropped = 0;         // 上限で捨てたイベントの数
    uint64_t detailsRecordedTotal = 0;  // 記録した判断の内訳の総数
    uint64_t detailsDropped = 0;        // 上限で捨てた判断の内訳の数
    uint64_t anomaliesRaised = 0;       // 検出した異常の数
    size_t eventCount = 0;              // 今、持っているイベントの数
    size_t detailCount = 0;             // 今、持っている判断の内訳の数
    size_t eventCapacity = 0;
    size_t detailCapacity = 0;
    size_t estimatedBytes = 0;          // 記録が使っているメモリの目安
    uint64_t oldestEventId = 0;         // 今残っている一番古いイベントの番号(無ければ0)
};

// ---------------------------------------------------------------------------
// 不変条件のチェック(純粋な関数。単体テストできる)
// ---------------------------------------------------------------------------

// チェックに必要な、外の事実(オブジェクトが有るか、など)。DemandSystem が集めて渡す。
struct SimInvariantInput
{
    bool homeObjectExists = true;       // homeObjectIdのオブジェクトが有るか(homeObjectIdが0なら、見ない)
    bool targetObjectExists = true;     // targetFacilityIdのオブジェクトが有るか(targetFacilityIdが0なら、見ない)
    bool markerPositionFinite = true;   // 見た目のオブジェクトの位置が、有限の値か
    const ResidentSimSettings* settings = nullptr; // 満足度の上限などに使う(nullptrなら、既定の設定)
};

// 住民の状態が壊れていないかを調べ、違反しているものを、SimAnomalyBitの論理和で返す(0=問題なし)。
// 誤検出を避けるため、状態ごとに「設計上ありえる状態」は違反にしない(例: 帰宅中の住民は、行き先が0でもよい)。
uint32_t CheckResidentInvariants(const ResidentAgent& agent, const SimInvariantInput& input);

// ---------------------------------------------------------------------------
// 記録の窓口
// ---------------------------------------------------------------------------

class SimDebugRecorder
{
public:
    static constexpr size_t kDefaultEventCapacity = 20000;  // 全住民の重要イベントの上限
    static constexpr size_t kDefaultDetailCapacity = 2000;  // 判断の内訳(候補ごとの計算)の上限(1件は、候補が数軒で約1KB。実測は学習メモ)

    // 記録のON/OFF。OFFの間は、何も記録しない(値を渡されても、すぐ戻る)。既定はOFF。
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // 容量を変える(今より小さくしたら、古いものを捨てる)。
    void SetCapacity(size_t eventCapacity, size_t detailCapacity);

    // 新しい「世界」を始める(ロード・リセット)。記録を全て消し、runIdを1つ進める。通し番号は戻さない。
    void StartNewRun();

    // ---- 書き込み(OFFの時は何もしない。戻り値は0) ----

    // イベントを追加する。eventId・runId・outingIdは、ここで付ける。
    //  ・OutingRoll と StrandedRecovered で新しい外出が始まる。
    //  ・OutingBlocked / ReturnHome / BecameStranded / BecameHomeless / 主目的の FacilitySelect の失敗 の後で、外出が終わる。
    uint64_t AddEvent(SimEvent event);

    // 判断の内訳を追加する。detailIdを付けて、そのidを返す(イベントのdetailIdに入れる)。
    uint64_t AddDetail(SimDecisionDetail detail);

    // 外出判定の結果を数える(不発もここに数える。イベントにはしない)。
    void NoteOutingRoll(uint32_t residentId, OutingChanceBranch branch, float chance, float roll, bool hit, int week, float hour);

    // 新しく外出を考える条件(就寝中・遅い時刻など)を、記録する。
    void NoteTickGate(const SimTickGate& gate);

    // 住民の、今違反している異常のビットを渡す。新しく違反になったビット(前回は違反していなかったもの)を返す。
    uint32_t UpdateAnomalyState(uint32_t residentId, uint32_t currentMask);

    // ---- 読み取り(UI用) ----
    const std::deque<SimEvent>& Events() const { return m_events; }
    const SimDecisionDetail* FindDetail(uint64_t detailId) const;
    const SimResidentRecord* FindResident(uint32_t residentId) const;
    const SimTickGate& GetTickGate() const { return m_tickGate; }
    uint32_t GetRunId() const { return m_runId; }
    uint32_t CurrentOutingId(uint32_t residentId) const;
    SimRecorderStats GetStats() const;

    // この住民の、直近のイベントを、新しい順に最大count件、集める(イベントの数に対して小さいcountで使う)。
    std::vector<const SimEvent*> RecentEventsOf(uint32_t residentId, size_t count) const;

private:
    void TrimEvents();
    void TrimDetails();

    bool m_enabled = false;
    size_t m_eventCapacity = kDefaultEventCapacity;
    size_t m_detailCapacity = kDefaultDetailCapacity;

    uint32_t m_runId = 1;
    uint64_t m_nextEventId = 1;
    uint64_t m_nextDetailId = 1;
    uint32_t m_nextOutingId = 1;

    std::deque<SimEvent> m_events;
    std::deque<SimDecisionDetail> m_details;
    std::unordered_map<uint32_t, SimResidentRecord> m_residents;
    SimTickGate m_tickGate;

    // 集計(世界を作り直すと0に戻る)。
    uint64_t m_eventsRecordedTotal = 0;
    uint64_t m_eventsDropped = 0;
    uint64_t m_detailsRecordedTotal = 0;
    uint64_t m_detailsDropped = 0;
    uint64_t m_anomaliesRaised = 0;
};

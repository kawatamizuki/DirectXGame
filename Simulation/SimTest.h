#pragma once
#include <cstdint>
#include <vector>

// 行動テスト(段階3): 住民に、意図した行動を「安全なタイミングで」実行させ、結果を確かめるための、データ型と、小さな計算。
//
//  ・指示(SimTestCommand)は、画面が作って、DemandSystem に渡す。DemandSystem は、指示を「計画(SimTestPlan)」にして持ち、
//    次の安全な意思決定のタイミング(外出判定・立ち寄り判定・更新の最初)で、1回だけ消化する。画面が、住民の状態を直接変えることは無い。
//  ・「通常ルール」: 強制するのは、確率の抽選の結果だけ。時刻・体力・到達できるか・使用済みの施設・経路探索は、普通どおり通す。
//    条件に合わない時は、実行せず、「拒否された理由」を返す。
//  ・「施設を指定」: 確率の抽選に加えて、重み付きの施設の抽選を、指定した施設に決める。ただし、その施設が、除外された(到達できない・体力不足・
//    使用済み・範囲外)時は、実行せず、拒否する(経路の欠落・壊れた参照などの、安全のための確認は、残す)。
//  ・強制中のイベントには、テストの番号(SimEvent::testId)を付ける。通常のプレイのイベントと、区別できる。
//  ・ゲームの世界の状態は、強制した分だけ変わる(施設の来客・売上・所持金にも、反映される。統計から外すのは、段階4)。
//  ・画面・ゲームのオブジェクトに依存しない(単体テストできる)。

// 何を実行するか。
enum class SimTestAction : uint8_t
{
    ForceOuting,    // 外出させる: 家にいる住民の、次の外出判定の確率を、必ず当選にする(+ 外出の途中の立ち寄りの当選も、回数だけ強制できる)
    ForceStopover,  // 立ち寄りを当選させる: 外出中の住民の、次の立ち寄り判定の確率を、回数だけ、必ず当選にする
    ReturnHome,     // 帰宅させる: 外出中(向かう途中・施設の中)の住民を、今の位置から、家へ帰らせる(利用はしない。売上・回復なし)
    Redirect,       // 行き先を変える: 施設へ向かう途中の住民の、行き先を、別の施設に変える(今の位置から、経路を作り直す)
    SetStamina      // 体力を決める: 体力を、指定した値にする(0〜今の上限。体力ぎりぎりの検証の準備)
};

// どこまで強制するか。
enum class SimTestMode : uint8_t
{
    Normal,         // 通常ルール: 確率の抽選だけを、当選にする。施設は、普通の重み付きの抽選で選ぶ
    ForceFacility   // 施設を指定: 抽選の結果を無視して、指定した施設を選ぶ(除外された施設なら、拒否)
};

// 誰に実行するか。
enum class SimTestTarget : uint8_t
{
    Selected,       // 今、住民デバッグで見ている住民
    Rect,           // 指定した四角(ワールドのXZ)の中にいる住民(実行ボタンを押した時の位置で選ぶ)
    All             // 全住民
};

// 計画の状態。
enum class SimTestStatus : uint8_t
{
    Pending,        // 待機中: まだ、消化していない(次の安全なタイミングを待っている)
    Running,        // 実行中: 消化した。外出が終わるまで(立ち寄りの強制が残っていれば、その判定まで)続く
    Succeeded,      // 成功: 指示どおりに実行できた
    Rejected,       // 拒否: 条件に合わず、実行できなかった(理由は、SimTestReject)。普通のルールの結果で、異常ではない
    Cancelled,      // 取消: プレイヤーが取り消した
    Expired         // 期限切れ: 期限(次の週)までに、タイミングが来なかった・世界が変わった
};

// 実行できなかった理由。
enum class SimTestReject : uint8_t
{
    None,
    AlreadyTesting,         // この住民には、別のテストが実行中・待機中
    ResidentNotFound,       // 住民が見つからない
    NotAtHome,              // (外出)家にいない
    NotOut,                 // (立ち寄り・帰宅)外出中でない
    NotGoingOut,            // (行き先変更)施設へ向かう途中でない
    HomeNoRoad,             // (外出)自宅の近くに道路が無い
    NoFacilityInCategory,   // (外出)目的のカテゴリの施設が無い
    NoPickable,             // (外出)候補は有ったが、全て除外された(到達不可・体力不足など)
    FacilityNotFound,       // 指定した施設が無い・施設でない・候補に無い
    FacilityNotEligible,    // 指定した施設は、除外された(detail=CandidateReject の値)
    SameFacility,           // (行き先変更)今の行き先と同じ
    NoRoute,                // 経路が無い(道路でつながっていない)
    NotEnoughStamina,       // 体力が足りない
    StopBlocked,            // (立ち寄り)立ち寄りを考えない条件(detail=StopoverBlock の値)
    StopNoRoad,             // (立ち寄り)家か今の施設の近くに道路が無い
    StopHomeMissing,        // (立ち寄り)帰る家が見つからない
    StopNoCandidates,       // (立ち寄り)同じカテゴリの他の施設が無い
    StopNoPickable,         // (立ち寄り)候補は有ったが、全て除外された
    StopFoodChain,          // (立ち寄り)食事系が続くので、取りやめた(これは、強制しない確率の判定)
    NotConsumed,            // 外出が終わるまで、強制する判定が来なかった(detail=残った回数)
    WorldChanged,           // ロード・リセットで、住民が入れ替わった
    Superseded              // 実行中だった計画が、新しい指示で置き換えられた(取消扱い)
};

// 指示(画面が作る)。
struct SimTestCommand
{
    SimTestAction action = SimTestAction::ForceOuting;
    SimTestMode mode = SimTestMode::Normal;
    SimTestTarget target = SimTestTarget::Selected;

    // target==Rect の時の、四角(ワールドのXZ。min<=max)。
    float rectMinX = 0.0f;
    float rectMinZ = 0.0f;
    float rectMaxX = 0.0f;
    float rectMaxZ = 0.0f;

    int maxCount = 50;              // 一度に実行する人数の上限(超えた分は、実行しない)
    uint32_t facilityId = 0;        // mode==ForceFacility の時に選ぶ施設(GameObject::id)。行き先変更でも使う
    int forcedStopovers = 0;        // 立ち寄りの確率を、何回、必ず当選にするか(ForceOuting は0〜上限、ForceStopover は1以上)
    float staminaValue = 100.0f;    // SetStamina の値
};

// 計画1件(住民1人ぶん)。待機中・実行中の物を、DemandSystem が持ち、終わった物は、結果の一覧として残す。
struct SimTestPlan
{
    uint32_t testId = 0;            // テストの番号(イベントに付く。通し番号)
    uint32_t batchId = 0;           // 同時に出した指示のまとまり(取消に使う)
    uint32_t residentId = 0;
    SimTestAction action = SimTestAction::ForceOuting;
    SimTestMode mode = SimTestMode::Normal;
    SimTestStatus status = SimTestStatus::Pending;
    SimTestReject reject = SimTestReject::None;
    int rejectDetail = 0;           // rejectの補足(CandidateReject・StopoverBlock の値、残った回数など)
    uint32_t facilityId = 0;        // 指定した施設(0=無し)
    bool forceOuting = false;       // 外出判定の確率を、まだ強制していない(消化したら false)
    int forcedStopovers = 0;        // 立ち寄りの確率を、あと何回、強制するか
    int stopoversRequested = 0;     // 指示された立ち寄りの回数
    int stopoversStarted = 0;       // 実際に、立ち寄りを始めた回数
    float staminaValue = 0.0f;
    int createdWeek = 0;
    float createdHour = 0.0f;
    int finishedWeek = 0;
    float finishedHour = 0.0f;
    uint64_t firstEventId = 0;      // 消化した時に記録した TestCommand イベントの番号(履歴へ移るのに使う。記録がOFFなら0)
};

// 指示を出した結果(画面に出す)。
struct SimTestSubmitResult
{
    uint32_t batchId = 0;
    int targeted = 0;               // 対象になった人数
    int created = 0;                // 計画を作った人数(待機中になった)
    int rejectedNow = 0;            // 出した時点で、拒否した人数(別のテストが実行中など)
    bool truncated = false;         // 人数の上限で、対象を切り詰めたか
};

// 一度に出せる人数の、絶対の上限。
constexpr int kSimTestMaxBatch = 500;

inline bool SimTestIsActive(SimTestStatus status) { return status == SimTestStatus::Pending || status == SimTestStatus::Running; }

// 対象を選ぶための、住民1人ぶんの情報(位置は、今の見た目のオブジェクトの位置)。
struct SimTestResidentPoint
{
    uint32_t residentId = 0;
    float x = 0.0f;
    float z = 0.0f;
};

// 指示の対象(住民のid)を、並びを保って選ぶ。Selected は selectedResidentId(0なら空)、Rect は四角の中(境界を含む)、All は全員。
// maxCount(1〜kSimTestMaxBatch に収める)を超えた分は、切り捨てて、*truncated をtrueにする。
std::vector<uint32_t> SelectTestTargets(const std::vector<SimTestResidentPoint>& residents, const SimTestCommand& command,
                                        uint32_t selectedResidentId, bool* truncated = nullptr);

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

class GameObject;
class RoadSystem;
class IOccupancyGrid;
class DemandSystem;

// フレームごとの処理時間・オブジェクト数・メモリ使用量を計測してImGuiに表示する簡易プロファイラ。
// PROFILE_SCOPE("名前") をスコープの先頭に置くと、そのスコープを抜けるまでの時間を計測できる。
// 「道路グラフを毎ティック作り直す処理が何msかかっているか」のような判断を、
// 感覚ではなく実測で行えるようにするためのもの。
class Profiler
{
public:
    static void BeginFrame(); // 毎フレーム先頭で呼ぶ(前フレームの計測結果をクリアする)
    static void RecordSection(const char* name, float milliseconds);

    // ImGuiパネル表示。objectsはkind別の内訳表示に使う。
    // roadSystem/occupancy/demandはメモリ内訳(建物・道路・フィールド・経路探索)の推定値表示に使う
    // (推定値であることの説明も含めてImGui側に表示する)。
    static void DrawDebugUI(
        const std::vector<GameObject>& objects,
        const RoadSystem& roadSystem,
        const IOccupancyGrid& occupancy,
        const DemandSystem& demand);

private:
    static std::unordered_map<std::string, float> s_sectionTimesMs;
};

// スコープを抜ける時に自動でProfiler::RecordSectionを呼ぶRAIIヘルパー。
// 直接使わず、PROFILE_SCOPEマクロ経由で使う。
class ScopedProfileSection
{
public:
    explicit ScopedProfileSection(const char* name);
    ~ScopedProfileSection();

private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start;
};

// __LINE__を正しく展開するための2段階マクロ(標準的なイディオム)。
#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)

// 計測したいスコープの先頭に置く。スコープを抜けるまでの時間が自動で記録される。
#define PROFILE_SCOPE(name) ScopedProfileSection PROFILE_CONCAT(_profScope, __LINE__)(name)

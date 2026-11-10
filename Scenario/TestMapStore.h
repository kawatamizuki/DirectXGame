#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// テストマップの管理(画面・ゲームの状態に依存しない。単体テストできる)。
// テストマップ = ゲーム内で、手作業で作った街を、名前を付けて保存した物。何度でも、同じ初期状態から検証するための「元ファイル」。
//
//  ・保存形式: 既存のセーブ(ScenarioManager が書く JSON)に、メタ情報 "testMap"(名前・説明・検証目的・注目する指標・作成日時・版)を足しただけ。
//    ゲームの読み込みは、知らない項目を無視するので、そのまま、普通のセーブとして読める。
//  ・置き場所: 正式に採用した物は TestMaps/(Git管理)、作成途中・試作は TestMaps/Drafts/(Git管理外)。
//  ・ファイル名(id)は、半角の英数字・「_」「-」だけ(パスの文字コードの問題を避ける)。日本語の名前は、メタ情報の「名前」に持つ。
//  ・安全: 元ファイルを書き換えるのは、このクラスの操作(新規保存・メタ情報の更新・名前変更・移動・削除)だけ。通常のセーブ(ScenarioManager::Save)は、
//    TestMaps/ の中へ書けない(PathGuard)。新規保存は、既存の名前を上書きしない。ファイルは、一時ファイルに書いてから置き換える。
//  ・初期状態の指紋: 街の部分(メタ情報を除いた内容)のFNV-1aハッシュ。名前や説明を直しても変わらない。同じ指紋 = 同じ初期状態。
struct TestMapMeta
{
    std::string name;                       // 表示名(日本語でよい)
    std::string description;                // 説明(どんな街か)
    std::string purpose;                    // 検証目的(何を確かめるための街か)
    std::vector<std::string> focusMetrics;  // 注目する指標(例: "エリアの評判", "店舗別の売上")
    std::string createdAt;                  // 作成日時(保存した時に入る)
    std::string gameBuild;                  // 保存した時のゲームのビルド(参考)
};

enum class TestMapFolder
{
    Official,   // 正式に採用した物(TestMaps/。Git管理)
    Draft       // 作成途中・試作(TestMaps/Drafts/。Git管理外)
};

struct TestMapEntry
{
    std::string id;                         // ファイル名(拡張子なし)
    TestMapFolder folder = TestMapFolder::Official;
    std::string path;                       // ファイルのパス
    TestMapMeta meta;
    std::uint64_t fingerprint = 0;          // 初期状態の指紋(メタ情報を除いた街の内容)
    std::uint64_t sizeBytes = 0;
    bool valid = false;                     // 読めたか(壊れたファイルも、一覧には出す)
    std::string error;                      // validがfalseの時の理由
};

// 街の入出力(ゲーム側が渡す)。このクラスは、ScenarioManager を知らない。
struct TestMapWorldIO
{
    std::function<bool(std::string& bytes, std::string& message)> capture;          // 今の街を、セーブデータの文字列にする
    std::function<bool(const std::string& path, std::string& message)> loadFile;    // ファイルから、街を置き換える(既存のセーブと同じ検証)
};

class TestMapStore
{
public:
    static constexpr const char* kDefaultRoot = "TestMaps";    // ScenarioManager::kTestMapsDirectory と同じ(通常のセーブは、ここに書けない)
    static constexpr const char* kDraftSubdirectory = "Drafts";
    static constexpr int kSchemaVersion = 1;

    explicit TestMapStore(std::string root = kDefaultRoot);

    void SetWorldIO(TestMapWorldIO io) { m_io = std::move(io); }

    const std::string& Root() const { return m_root; }
    std::string DraftDirectory() const;
    std::string DirectoryOf(TestMapFolder folder) const;

    // id(ファイル名)の検査。半角の英数字・「_」「-」・1〜48文字・先頭は英数字・予約名(CON/NUL/COM1…・Drafts)でない。
    static bool IsValidId(const std::string& id, std::string* why = nullptr);
    // 街の部分の指紋(ファイルの内容から、メタ情報 "testMap" を除いて計算する)。JSONとして読めなければ false。
    static bool ComputeFingerprint(const std::string& fileBytes, std::uint64_t& out);
    // 街の「配置」の指紋: 道路・建物・青マス・飾り・商業エリアの形(位置・大きさ・周知の有無)だけ。住民・時計・評判など、時間で変わる状態は、含めない。
    // 「今の街が、読み込んだテストマップと、同じ配置か(建築・編集で、変えていないか)」を調べるのに使う。
    static bool ComputeLayoutFingerprint(const std::string& fileBytes, std::uint64_t& out);

    // 全てのテストマップ(正式 → 下書き。それぞれ id の順)。壊れたファイルは valid=false で出す。
    std::vector<TestMapEntry> List() const;
    bool Find(const std::string& id, TestMapEntry& out) const;

    // 今の街を、新しいテストマップとして保存する。既に同じ id(大文字小文字を区別しない。正式・下書きのどちらにも)があれば、断る(上書きしない)。
    bool SaveNew(const std::string& id, const TestMapMeta& meta, TestMapFolder folder, std::string& message);
    // メタ情報だけを書き直す(街の部分は、そのまま。指紋は変わらない)。createdAt・gameBuild は、元の値を残す。
    bool UpdateMeta(const std::string& id, const TestMapMeta& meta, std::string& message);
    // id(ファイル名)を変える。新しい id が既にあれば断る。
    bool Rename(const std::string& id, const std::string& newId, std::string& message);
    // 正式 <-> 下書き を移す(「正式に採用」「下書きへ戻す」)。移す先に同じ id があれば断る。
    bool Move(const std::string& id, TestMapFolder to, std::string& message);
    // 削除する(そのテストマップのファイルだけ)。
    bool Delete(const std::string& id, std::string& message);

    // テストマップを、街として読み込む(既存のセーブと同じ検証。失敗したら、今の街は壊れない)。成功したら、使用中の情報を更新する。
    bool Load(const std::string& id, std::string& message);

    // 使用中のテストマップ(Load した物。無ければ空)。
    struct ActiveMap
    {
        bool valid = false;
        std::string id;
        std::string name;
        std::uint64_t fingerprint = 0;         // 初期状態の指紋(街の全体)
        std::uint64_t layoutFingerprint = 0;   // 配置の指紋(読み込んだ時点)
    };
    const ActiveMap& Active() const { return m_active; }
    void ClearActive() { m_active = ActiveMap{}; }

private:
    // idのファイルを探す(正式 → 下書き)。見つかればtrue。両方にある(手でコピーした時など)なら、ambiguousがtrueになる(最初に見つかった方を返す)。
    bool ResolvePath(const std::string& id, std::string& path, TestMapFolder& folder, bool* ambiguous = nullptr) const;
    // 操作の対象を決める: 見つからない・両方にある(どちらか分からない)時は、falseを返し、理由をmessageに入れる。
    bool Locate(const std::string& id, std::string& path, TestMapFolder& folder, std::string& message) const;
    bool ReadEntry(const std::string& path, const std::string& id, TestMapFolder folder, TestMapEntry& out) const;
    bool WriteWithMeta(const std::string& path, const std::string& worldBytes, const TestMapMeta& meta, std::string& error) const;

    std::string m_root;
    TestMapWorldIO m_io;
    ActiveMap m_active;
};

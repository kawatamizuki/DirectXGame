#pragma once
#include "ISaveable.h"

// お金まわりの設定値(ゲームの内容が固まったら、ここの値を変えるか、ファイルから読む形に広げる)。
struct EconomyConfig
{
    // ゲームを新しく始めた時の所持金(円)。セーブデータに所持金が入っていれば、そちらが優先される。
    int startingCash = 1000;
};

// 所持金を管理する。建物・道路の建設費の支払いは、すべてここのSpendを通す
// (支払いの入り口を1つにしておくと、足りない時・上限・負の値の扱いが全体で揃う)。
// 収入(住民の税・オフィスの売上など)は、後からEarnを呼ぶだけで足せる(今は呼び元が無い)。
// ISaveableなので、"economy"の区画として所持金がセーブデータに保存・復元される。
class GameEconomy : public ISaveable
{
public:
    // 所持金の上限(桁あふれと、表示の幅が伸び続けるのを防ぐ)。
    static constexpr int kMaxCash = 999999999;

    // 今の所持金。
    int GetCash() const { return m_cash; }

    // amount円を払えるか。0円は常に払える。負の額は払えない(不正な呼び出しを通さない)。
    bool CanAfford(int amount) const { return amount >= 0 && amount <= m_cash; }

    // amount円を払う。払えない時は何も変えずfalseを返す。0円はtrue(何も変わらない)。
    bool Spend(int amount);

    // amount円を受け取る(上限で止まる)。0以下は何もしない。
    void Earn(int amount);

    // 所持金を直接決める(0〜上限に収める)。デバッグ用や、ロードの復元に使う。
    void SetCash(int cash);

    // 初期所持金の設定。ResetToDefault(新規ゲーム・全消去)で、この額に戻る。
    const EconomyConfig& GetConfig() const { return m_config; }
    void SetStartingCash(int startingCash);

    // ---- セーブ/ロード(ISaveable) ----
    // 保存するのは所持金だけ。初期所持金は「設定」なので保存しない(ゲーム側の設定が変われば、新規ゲームに反映される)。
    const char* SaveKey() const override { return "economy"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override;

    // 所持金・初期所持金を操作するデバッグ用ImGuiパネル(「Economy」ウィンドウ)。
    void DrawDebugUI();

private:
    // 0〜kMaxCashに収める。
    static int ClampCash(long long cash);

    EconomyConfig m_config;
    int m_cash = EconomyConfig().startingCash;
};

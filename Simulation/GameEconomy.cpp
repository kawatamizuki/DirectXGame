#include "GameEconomy.h"
#include <cmath>
#include "imgui.h"

int GameEconomy::ClampCash(long long cash)
{
    if (cash < 0)
    {
        return 0;
    }
    if (cash > kMaxCash)
    {
        return kMaxCash;
    }
    return static_cast<int>(cash);
}

bool GameEconomy::Spend(int amount)
{
    if (!CanAfford(amount))
    {
        return false;
    }
    m_cash -= amount;
    return true;
}

void GameEconomy::Earn(int amount)
{
    if (amount <= 0)
    {
        return;
    }
    // intの足し算が桁あふれしないよう、広い型で計算してから上限に収める。
    m_cash = ClampCash(static_cast<long long>(m_cash) + static_cast<long long>(amount));
}

void GameEconomy::SetCash(int cash)
{
    m_cash = ClampCash(cash);
}

void GameEconomy::SetStartingCash(int startingCash)
{
    m_config.startingCash = ClampCash(startingCash);
}

void GameEconomy::Save(Json& section, const SaveContext& /*context*/) const
{
    section.Set("cash", m_cash);
}

void GameEconomy::Load(const Json& section, int /*version*/, const LoadContext& /*context*/)
{
    // 足りない項目・数値でない項目・NaN/無限大は、初期所持金にする。範囲外の値は0〜上限に丸める
    // (壊れた/手書きのファイルでも落ちず、負の所持金にもならない)。
    const Json* cashValue = section.Find("cash");
    if (!cashValue || !cashValue->IsNumber() || !std::isfinite(cashValue->AsNumber()))
    {
        m_cash = m_config.startingCash;
        return;
    }

    double cash = cashValue->AsNumber();
    // doubleのままlong longへ変換すると巨大な値で未定義動作になるので、先に範囲で打ち切る。
    if (cash < 0.0)
    {
        cash = 0.0;
    }
    if (cash > static_cast<double>(kMaxCash))
    {
        cash = static_cast<double>(kMaxCash);
    }
    m_cash = ClampCash(static_cast<long long>(cash));
}

void GameEconomy::ResetToDefault()
{
    m_cash = m_config.startingCash;
}

void GameEconomy::DrawDebugUI()
{
    // 他の常設パネル(Time/Build/City Stats/Profiler/Log)と重ならない位置に初回だけ置く。
    ImGui::SetNextWindowPos(ImVec2(490.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Economy");

    ImGui::Text("Cash: %d", m_cash);

    int cash = m_cash;
    if (ImGui::InputInt("Set cash", &cash, 100, 1000))
    {
        SetCash(cash);
    }

    ImGui::Separator();

    // 初期所持金(新規ゲーム・Resetで戻る額)。保存はされない設定値。
    int startingCash = m_config.startingCash;
    if (ImGui::InputInt("Starting cash", &startingCash, 100, 1000))
    {
        SetStartingCash(startingCash);
    }
    if (ImGui::Button("Reset to starting cash"))
    {
        ResetToDefault();
    }
    ImGui::SameLine();
    if (ImGui::Button("+1000"))
    {
        Earn(1000);
    }

    ImGui::End();
}

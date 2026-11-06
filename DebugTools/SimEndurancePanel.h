#pragma once

class SimEndurance;

// Editorの「耐久試験」パネル(段階6)。長い期間を、画面を描かずに一気に進めて、異常・負荷・決定論を調べる。
// データは SimEndurance から読むだけ。ENABLE_EDITOR のビルドだけが呼ぶ。
class SimEndurancePanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    // canStart: 今、試験を始められるか(ゲームシーンのPlayingで、他の時間の制御が動いていない)。whyNot: 始められない理由(canStartがfalseの時)。
    void Draw(SimEndurance& endurance, bool canStart, const char* whyNot);

private:
    bool* m_open = nullptr;
};

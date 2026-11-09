#pragma once

class AreaLedger;
class CommercialAreaManager;
class GameClock;

// Editorの「商業エリア」パネル(P2-A)。区画の設定(所属のしきい値・計画規模・進捗の段階・猶予・区画の大きさ)を、実行中に調整し、
// 区画ごとの状態(公開・周知した週・店舗数・段階・所属する施設と、敷地の何%が区画の中か・履歴)と、点検の結果(重なり・二重所属)、撤回の記録を見る。
// 設定は保存しない(初期値は CommercialAreaSettings)。ENABLE_EDITOR のビルドだけが呼ぶ。
class AreaDebugPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    void Draw(CommercialAreaManager& areas, const GameClock& clock, const AreaLedger& ledger);

private:
    bool* m_open = nullptr;
};

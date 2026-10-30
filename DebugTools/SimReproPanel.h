#pragma once
#include <string>

class DemandSystem;
class SimReproController;

// Editorの「乱数・再現」パネル(段階5)。
//  ・乱数の取り方(通常=ロードで再抽選 / 固定シード / セーブした状態を復元)と、今の種・内部状態を見せる。
//  ・再現セッション(記録 → 再生 → 指紋の比較)の操作と、結果。
// データは DemandSystem と SimReproController から読むだけ。ENABLE_EDITOR のビルドだけが呼ぶ。
class SimReproPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    // timelineEnabled: タイムラインがONの間は、記録を始められない(どちらも、時間の進め方を決めるため)。
    void Draw(DemandSystem& demand, SimReproController& repro, bool timelineEnabled);

private:
    bool* m_open = nullptr;
    int m_replayRandom = 0;     // 0=乱数の状態を復元して再生 1=再抽選して再生
    char m_segmentName[48] = "segment1";   // 保存する区間の名前(ファイル名)
    int m_segmentSelect = -1;               // 読み込む区間(一覧の番号)
    std::string m_segmentMessage;           // 保存・読み込みの結果
};

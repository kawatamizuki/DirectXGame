#pragma once
#include <map>
#include <string>

class AudioSystem;

// エディタの「Audio」パネル: 音の一覧と、音量の調整。
//  - 上: 全体・BGM・効果音の音量(0〜100)。ゲーム内メニューの「オプション」と同じ値(ミキサー)を動かす。
//  - 一覧: 音の表(Sounds/sounds.json)の全ての音。音ごとの音量(%)を、数字(+/-ボタン・入力)とスライダーで調整でき、
//    ▶で試し聴きできる。変えた値はすぐ反映される(鳴っている音にも効く)。
//  - 「音の表に保存」: 変えた音量を Sounds/sounds.json へ書き戻す(書き戻さない限り、ファイルは変わらない)。
// 開いていない間は、何も処理しない。ゲームの動作には関わらない(開発用)。
class AudioPanel
{
public:
    // audio: 調整する音の窓口。tablePath: 音の表のファイル(保存先)。
    void Initialize(AudioSystem* audio, const std::string& tablePath);

    // Editorメニューの「パネル」と連動する開閉の旗(Gameが渡す。渡されなければ、常に閉じている)。
    void SetOpenFlag(bool* openFlag) { m_openFlag = openFlag ? openFlag : &m_fallbackFlag; }

    // 毎フレーム、ImGuiのフレーム内で呼ぶ。
    void DrawDebugUI();

private:
    // 音の表の音量が、最後に保存した(または読んだ)値から変わっているか。
    bool HasUnsavedChanges() const;
    // 今の音量を、「保存済み」として覚える。
    void RememberSavedVolumes();

    AudioSystem* m_audio = nullptr;
    std::string m_tablePath;
    std::map<std::string, float> m_savedVolumes;    // id → 最後に保存した音量
    std::string m_status;                           // 直前の保存の結果
    bool m_fallbackFlag = false;
    bool* m_openFlag = &m_fallbackFlag;
};

#pragma once
#include <string>

class AudioMixer;

// プレイヤーが設定する音量(ゲーム内メニューの「オプション」)。0〜100の整数で持つ。
// マスター(全体)、BGM、効果音(SE)の3つ。ファイル(Settings/audio.json)に保存して、次の起動でも保つ。
// 音ごとの音量(素材のばらつきをそろえる値)は別物で、音の表(Sounds/sounds.json)にある。
struct AudioSettings
{
    static constexpr int kMin = 0;
    static constexpr int kMax = 100;
    static constexpr const char* kDefaultPath = "Settings/audio.json";

    int master = kMax;      // 全体の音量
    int bgm = kMax;         // 曲の音量
    int se = kMax;          // 効果音の音量

    // 0〜100に収める。
    static int Clamp(int value);

    // ファイルから読む。無い・壊れている時は、何も変えずにfalse。足りない項目・範囲外の値は、既定値・収めた値にする。
    bool LoadFromFile(const std::string& path);

    // ファイルへ書く(一時ファイルに書いてから置き換える)。失敗したらfalse。
    bool SaveToFile(const std::string& path) const;

    // ミキサーへ反映する / ミキサーの今の値を読む。
    void ApplyTo(AudioMixer& mixer) const;
    static AudioSettings FromMixer(const AudioMixer& mixer);

    bool operator==(const AudioSettings& other) const
    {
        return master == other.master && bgm == other.bgm && se == other.se;
    }
    bool operator!=(const AudioSettings& other) const { return !(*this == other); }
};

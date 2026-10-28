#pragma once
#include <string>

// 音まわりで共通に使う型。

// 音の種類。種類ごとに、音量を分けて調整できる(システム画面のスライダー・ミュート)。
enum class SoundCategory
{
    Bgm = 0,    // 曲(長い音。ループすることが多い)
    Se = 1      // 効果音(短い音)
};
constexpr int kSoundCategoryCount = 2;

// 音の表・設定ファイルで使う、種類の名前(数値ではなく名前で持つ。並びを変えても壊れないように)。
inline const char* SoundCategoryKey(SoundCategory category)
{
    return (category == SoundCategory::Bgm) ? "Bgm" : "Se";
}

inline bool SoundCategoryFromKey(const std::string& key, SoundCategory& out)
{
    if (key == "Bgm") { out = SoundCategory::Bgm; return true; }
    if (key == "Se")  { out = SoundCategory::Se;  return true; }
    return false;
}

// 「何をミュートするか」。ゲームの都合で、特定のタイミングに、ピンポイントで指定できる。
struct MuteTarget
{
    enum class Kind
    {
        All,        // 全ての音(マスターのミュートとは別の、ゲームの都合のミュート)
        Category,   // 種類ごと(BGM全部・SE全部)
        Sound,      // 特定の1つの音(音のidで指定)
        Tag         // 同じタグの音(例: "ui"のタグを付けた効果音をまとめて)
    };

    Kind kind = Kind::All;
    SoundCategory category = SoundCategory::Bgm;    // kind==Categoryの時だけ使う
    std::string name;                               // kind==Soundなら音のid、kind==Tagならタグ名

    static MuteTarget All() { return MuteTarget{}; }
    static MuteTarget Category(SoundCategory category)
    {
        MuteTarget target;
        target.kind = Kind::Category;
        target.category = category;
        return target;
    }
    static MuteTarget Sound(const std::string& soundId)
    {
        MuteTarget target;
        target.kind = Kind::Sound;
        target.name = soundId;
        return target;
    }
    static MuteTarget Tag(const std::string& tag)
    {
        MuteTarget target;
        target.kind = Kind::Tag;
        target.name = tag;
        return target;
    }
};

// ミュートの要求の番号。ミュートを解除する時に、この番号で指定する(0は無効)。
using MuteHandle = int;
constexpr MuteHandle kInvalidMuteHandle = 0;

// 鳴っている1つの音(再生中の音)の番号(0は無効)。
using VoiceId = int;
constexpr VoiceId kInvalidVoiceId = 0;

#pragma once
#include <vector>
#include "AudioTypes.h"
#include "SoundTable.h"

// 距離による音の大きさ(0〜1)。distance(聞く位置から音の位置まで)が、
//   minDistance以下 → 1(そのまま)
//   maxDistance以上 → 0(聞こえない)
//   その間 → 1から0へ、直線的に小さくなる。rolloffが大きいほど、近いうちに急に小さくなる(1-tのrolloff乗)
// 範囲がおかしい値(最大が最小以下など)でも、壊れない値を返す。
float DistanceGain(float distance, float minDistance, float maxDistance, float rolloff = 1.0f);

// 音量の層と、ミュートの要求を管理する(音の出力そのものは知らない。ライブラリ無しでテストできる)。
//
// 最終的な音の大きさ = マスター × 種類(BGM/SE)の音量 × 音ごとの音量 × (ミュートなら0) × 距離による減衰 × フェード
// このクラスは、「マスター × 種類 × 音ごと」(BaseGain)と、「ミュートか」(IsMuted)を答える。
// 距離とフェードは、鳴っている音ごとの状態なので、AudioSystemが掛ける。
//
// ミュートには、2種類ある:
//  - ユーザーのミュート(システム画面のスイッチ。マスターと、種類ごと。保存される想定)
//  - ゲームの都合のミュート(AddMute。「このタイミングだけ、BGMを消す」など。保存しない)
class AudioMixer
{
public:
    // ---- 音量(0〜1。範囲外は収める) ----
    void SetMasterVolume(float volume) { m_masterVolume = Clamp01(volume); }
    float GetMasterVolume() const { return m_masterVolume; }
    void SetCategoryVolume(SoundCategory category, float volume) { m_categoryVolume[Index(category)] = Clamp01(volume); }
    float GetCategoryVolume(SoundCategory category) const { return m_categoryVolume[Index(category)]; }

    // ---- ユーザーのミュート(システム画面のスイッチ) ----
    void SetMasterMuted(bool muted) { m_masterMuted = muted; }
    bool IsMasterMuted() const { return m_masterMuted; }
    void SetCategoryMuted(SoundCategory category, bool muted) { m_categoryMuted[Index(category)] = muted; }
    bool IsCategoryMuted(SoundCategory category) const { return m_categoryMuted[Index(category)]; }

    // ---- ゲームの都合のミュート ----
    // targetを、ミュートする。durationSecondsが0以下なら、RemoveMuteで解除するまで続く。正なら、その秒数のあと自動で解除される。
    // 同じ対象に、複数の要求が重なっても、全部が解除されるまで、ミュートのまま(別々の理由で、ミュートした時に、
    // 片方が解除しても、もう片方の分は続く)。戻り値は、解除に使う番号。
    MuteHandle AddMute(const MuteTarget& target, float durationSeconds = 0.0f);
    bool RemoveMute(MuteHandle handle);     // 解除できたらtrue(既に解除済み・無効な番号ならfalse)
    void ClearMutes();                      // ゲームの都合のミュートを、全て解除する
    int ActiveMuteCount() const { return static_cast<int>(m_mutes.size()); }

    // 時間つきのミュートの、残り時間を進めて、終わったものを解除する。毎フレーム呼ぶ。
    void Update(float deltaSeconds);

    // ---- 答えを返す ----
    // マスター × 種類 × 音ごとの音量(ミュート・距離・フェードは含まない)。
    float BaseGain(const SoundDefinition& sound) const;

    // 今、ミュートされているか(ユーザーのミュート、ゲームの都合のミュート、どちらでも)。
    bool IsMuted(const SoundDefinition& sound) const;

private:
    struct MuteRequest
    {
        MuteHandle handle = kInvalidMuteHandle;
        MuteTarget target;
        float remainingSeconds = 0.0f;  // 0以下なら、時間では解除されない
        bool timed = false;
    };

    static float Clamp01(float value);
    static int Index(SoundCategory category) { return static_cast<int>(category); }
    static bool Matches(const MuteTarget& target, const SoundDefinition& sound);

    float m_masterVolume = 1.0f;
    float m_categoryVolume[kSoundCategoryCount] = { 1.0f, 1.0f };
    bool m_masterMuted = false;
    bool m_categoryMuted[kSoundCategoryCount] = { false, false };

    std::vector<MuteRequest> m_mutes;
    MuteHandle m_nextHandle = 1;
};

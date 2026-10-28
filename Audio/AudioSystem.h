#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "AudioTypes.h"
#include "SoundTable.h"
#include "AudioMixer.h"
#include "IAudioBackend.h"

// ゲームが音を鳴らす時の、窓口。ゲームのコードは、このクラスだけを使う(miniaudioなどのライブラリは知らない)。
//
// 役割:
//  - 音の表(SoundTable)から、idで音を探して鳴らす
//  - 音量の層(マスター・BGM/SE・音ごと)、ミュート(ユーザーの設定・ゲームの都合のピンポイントのミュート)
//  - 距離による減衰(聞く位置=カメラ と、音の位置)
//  - BGMの切り替え(フェード)、同時に鳴らせる数の上限
// 計算が終わった「今の大きさ」だけを、IAudioBackend(実際に鳴らす部分)へ送る。
// 音が出せない環境(デバイス無し・初期化の失敗)でも、全ての呼び出しは安全で、無音のまま動く。
class AudioSystem
{
public:
    // ミュート・解除の時の、音の大きさの変化にかける時間(秒)。一瞬で0にすると、「プツッ」というノイズが出るため。
    static constexpr float kMuteFadeSeconds = 0.08f;

    // 実際に鳴らす部分を渡す(所有する)。nullptrなら、何も鳴らさない実装になる。渡した後に、Initializeを呼ぶ。
    void SetBackend(std::unique_ptr<IAudioBackend> backend);
    bool Initialize();      // バックエンドを準備する。音が出せなくてもfalseを返すだけ(ゲームは続けられる)
    void Shutdown();
    bool IsAvailable() const;

    // ---- 音の表 ----
    SoundTable& Table() { return m_table; }
    const SoundTable& Table() const { return m_table; }
    bool LoadTable(const std::string& path);    // 失敗しても、ゲームは続けられる(音の表が空になるだけ)

    // ---- 音量・ミュート ----
    AudioMixer& Mixer() { return m_mixer; }
    const AudioMixer& Mixer() const { return m_mixer; }

    // ---- 聞く位置(毎フレーム、カメラの位置を渡す) ----
    void SetListenerPosition(const DirectX::XMFLOAT3& position) { m_listener = position; }
    const DirectX::XMFLOAT3& GetListenerPosition() const { return m_listener; }

    // ---- 鳴らす ----
    // 効果音を鳴らす。positionを渡すと、位置を持つ音(spatial)は、聞く位置からの距離で小さくなる。
    // 表に無いid・ミュート中(繰り返さない音)・遠くて聞こえない・音が出せない時は、鳴らさず、kInvalidVoiceIdを返す。
    VoiceId PlaySe(const std::string& id, const DirectX::XMFLOAT3* position = nullptr);

    // 曲を鳴らす。今の曲があれば、fadeOutSecondsで消しながら、新しい曲をfadeInSecondsで出す。
    // 今鳴っている曲と同じなら、何もしない(最初からやり直さない)。鳴らせたらtrue。
    bool PlayBgm(const std::string& id, float fadeInSeconds = 1.0f, float fadeOutSeconds = 1.0f);
    void StopBgm(float fadeOutSeconds = 1.0f);
    const std::string& CurrentBgmId() const { return m_currentBgmId; }

    // 動く音源(住民など)の位置を更新する。
    void SetVoicePosition(VoiceId voice, const DirectX::XMFLOAT3& position);

    void StopSound(const std::string& id);      // そのidの音を、全て止める
    void StopAll();                             // 全ての音を止める

    // 毎フレーム呼ぶ。ミュートの残り時間・フェード・距離を反映して、鳴っている音の大きさを更新し、鳴り終わった音を片付ける。
    void Update(float deltaSeconds);

    // ---- 状態(表示・テスト用) ----
    int ActiveVoiceCount() const { return static_cast<int>(m_voices.size()); }
    int ActiveVoiceCount(const std::string& id) const;
    // voiceに、最後にバックエンドへ送った大きさ。鳴っていなければ-1。
    float GetVoiceGain(VoiceId voice) const;

private:
    struct Voice
    {
        VoiceId id = kInvalidVoiceId;       // バックエンドが付けた番号
        int defIndex = -1;                  // 音の表の、何番目か
        bool hasPosition = false;
        DirectX::XMFLOAT3 position{};
        float muteFactor = 1.0f;            // ミュートによる大きさ(0〜1)。ミュート・解除で、少しずつ変わる
        float fade = 1.0f;                  // フェードの大きさ(0〜1)
        float fadeTarget = 1.0f;
        float fadeRate = 0.0f;              // 1秒あたりに、フェードが変わる量
        bool stopWhenSilent = false;        // フェードで0になったら、止めるか(曲の切り替え・停止)
        float lastGain = -1.0f;             // 最後にバックエンドへ送った大きさ
    };

    // 音の表のdefIndex番の音について、このvoiceの今の大きさを求める。
    float ComputeGain(const Voice& voice, const SoundDefinition& def) const;

    // 音を鳴らし始める共通の処理。鳴らせなければ、nullptr。
    Voice* StartVoice(int defIndex, const DirectX::XMFLOAT3* position, float initialFade);

    void StopVoiceAt(size_t index);                                 // index番の音を止めて、一覧から外す
    Voice* FindVoice(VoiceId id);
    const Voice* FindVoice(VoiceId id) const;
    void WarnOnce(const std::string& key, const std::string& message);
    static void StartFade(Voice& voice, float target, float seconds, bool stopWhenSilent);

    std::unique_ptr<IAudioBackend> m_backend;
    SoundTable m_table;
    AudioMixer m_mixer;
    std::vector<Voice> m_voices;            // 鳴り始めた順(古いものが先頭)
    DirectX::XMFLOAT3 m_listener{};

    VoiceId m_bgmVoice = kInvalidVoiceId;
    std::string m_currentBgmId;
    std::set<std::string> m_warned;         // 同じ警告を、何度も出さないための記録
};

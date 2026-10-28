#pragma once
#include <string>
#include <vector>
#include "AudioTypes.h"

// 実際に音を鳴らす部分の、差し替え口。「このファイルを、この大きさで鳴らす」だけを知っている、小さなインターフェース。
// 音の表・音量の層・ミュート・距離の計算・フェードは、AudioSystem(ライブラリを知らない)が行い、
// ここには、計算が終わった「今の大きさ」だけが届く。だから、miniaudio以外(XAudio2など)に替えたくなっても、
// このインターフェースの実装を1つ作るだけで済む。
class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    // 音の出力を準備する。音のデバイスが無いなどで、失敗してもfalseを返すだけ(ゲームは、無音で続ける)。
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsAvailable() const = 0;       // 音を鳴らせる状態か

    // fileを、gain(0以上。1が元の大きさ)で、鳴らし始める。失敗したら、kInvalidVoiceId。
    // loopなら繰り返す。streamなら、少しずつ読みながら鳴らす(長い曲向き)。
    virtual VoiceId Play(const std::string& file, bool loop, bool stream, float gain) = 0;

    // 鳴っている音の大きさを変える(毎フレーム、距離・ミュート・フェードの結果を、ここへ送る)。
    virtual void SetGain(VoiceId voice, float gain) = 0;

    // 鳴っている音を止めて、片付ける。
    virtual void Stop(VoiceId voice) = 0;

    // まだ鳴っているか(繰り返さない音は、鳴り終わるとfalseになる)。
    virtual bool IsPlaying(VoiceId voice) const = 0;

    // メモリの上のデータ(mp3・wavのバイト列)を、「nameというファイル」として登録する。以後、Play(name, ...) で、
    // ディスクのファイルより先に、これが使われる(音のパック Sounds/sounds.pak の中身を鳴らすため)。
    // 登録できない実装は、falseを返す(何もしない)。
    virtual bool RegisterMemoryFile(const std::string& name, std::vector<unsigned char> data) { (void)name; (void)data; return false; }
};

// 何も鳴らさない実装。音が出せない環境でも、ゲームが落ちずに動くための、代わり。
class NullAudioBackend : public IAudioBackend
{
public:
    bool Initialize() override { return false; }
    void Shutdown() override {}
    bool IsAvailable() const override { return false; }
    VoiceId Play(const std::string&, bool, bool, float) override { return kInvalidVoiceId; }
    void SetGain(VoiceId, float) override {}
    void Stop(VoiceId) override {}
    bool IsPlaying(VoiceId) const override { return false; }
};

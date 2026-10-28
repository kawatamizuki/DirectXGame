#pragma once
#include <memory>
#include "IAudioBackend.h"

// miniaudio(External/miniaudio)で、実際に音を鳴らす実装。wav・mp3を読める。
// miniaudioのヘッダーは大きいので、ここでは読み込まず、中身はMiniaudioBackend.cpp(Impl)に隠している。
// 音の大きさ(距離・ミュート・フェードの結果)は、AudioSystemが計算して届ける。miniaudio自身の位置による音の処理
// (空間化)は、使わない(切ってある)。ルールをライブラリ無しでテストでき、ライブラリを替えやすくするため。
class MiniaudioBackend : public IAudioBackend
{
public:
    struct Options
    {
        // trueなら、音のデバイスを開かない(自動テスト用)。音は、ReadFramesで、波形として取り出す。
        bool noDevice = false;
        unsigned int channels = 2;          // noDeviceの時の、チャンネル数と、1秒あたりの標本の数
        unsigned int sampleRate = 48000;
    };

    MiniaudioBackend();
    explicit MiniaudioBackend(const Options& options);
    ~MiniaudioBackend() override;

    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

    bool Initialize() override;
    void Shutdown() override;
    bool IsAvailable() const override;
    VoiceId Play(const std::string& file, bool loop, bool stream, float gain) override;
    void SetGain(VoiceId voice, float gain) override;
    void Stop(VoiceId voice) override;
    bool IsPlaying(VoiceId voice) const override;

    // noDeviceの時だけ: 混ぜた波形(交互に並んだfloat、channels個で1フレーム)を、framesフレーム分、取り出す。
    // 戻り値は、取り出せたフレーム数。デバイスを開いている時は、0。
    unsigned int ReadFrames(float* output, unsigned int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    Options m_options;
};

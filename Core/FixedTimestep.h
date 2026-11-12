#pragma once
#include <cmath>
#include <cstdint>

// ゲーム内時間の固定刻みで、シミュレーションを何回進めるかを決める(アキュムレーター方式。Unity の FixedUpdate と同じ考え方)。
// 毎フレーム「実時間 × 倍率」を貯めて、1回の刻み(ゲーム内の秒)の分だけ取り出す。
//  ・FPS・倍率に関係なく、1回の更新で進むゲーム内の時間は、いつも同じ(stepGameSeconds)。同じ回数なら、同じ結果(決定論)。
//  ・倍率を上げると、回す回数が増えるだけ(刻みは変えない)。
//  ・1フレームとして受け付ける実時間に上限(maxFrameRealSeconds)、1フレームに回す回数に上限(maxStepsPerFrame)を設ける。
//    超えた分は捨てる(ゲームが一時的に遅くなるだけ。刻みは変えないので、決定論は崩れない)。
//    長く止まった後に、一気に追いつこうとして重くなり、さらに遅れる(死のスパイラル)のを防ぐ。
//  ・描画の補間用に、貯まりの残りの割合(Alpha)を出す。
// 画面・ゲーム本体に依存しない(単体テストできる)。1回のステップで何をするかは、呼ぶ側が決める。
// (テストマップ 段階4の前の、固定時間刻みへの移行の F1。まだゲーム本体からは使っていない)

// 固定刻みの設定。
struct FixedTimestepSettings
{
    double stepGameSeconds = 1.0 / 60.0;    // 1回の刻み(ゲーム内の秒)。1倍・60FPSの、今の1フレームと同じ長さ
    double maxFrameRealSeconds = 0.25;      // 1フレームとして受け付ける実時間の上限(秒)。これを超える停止(ウィンドウのドラッグなど)は、切り捨てる
    int maxStepsPerFrame = 240;             // 1フレームに回す回数の上限(4倍で、実時間1秒分)
};

class FixedTimestep
{
public:
    explicit FixedTimestep(const FixedTimestepSettings& settings = FixedTimestepSettings()) : m_settings(settings) {}

    // 1フレーム分の時間を貯めて、今回まわすステップの回数を返す。
    // realDeltaSeconds: 前のフレームからの実時間(秒)。timeScale: 倍率(0以下 = 停止。貯めない)。
    int Accumulate(double realDeltaSeconds, double timeScale)
    {
        if (!(realDeltaSeconds > 0.0) || !std::isfinite(realDeltaSeconds) || !(timeScale > 0.0) || !std::isfinite(timeScale))
        {
            return 0;
        }
        const double step = m_settings.stepGameSeconds;
        if (realDeltaSeconds > m_settings.maxFrameRealSeconds)
        {
            m_droppedGameSeconds += (realDeltaSeconds - m_settings.maxFrameRealSeconds) * timeScale;
            realDeltaSeconds = m_settings.maxFrameRealSeconds;
        }
        m_accumulator += realDeltaSeconds * timeScale;

        // 割り算の丸めで、ちょうど1回分なのに0回にならないよう、ごくわずかに余裕を持たせる。
        double count = std::floor(m_accumulator / step + 1e-9);
        if (count > static_cast<double>(m_settings.maxStepsPerFrame))
        {
            const double over = count - static_cast<double>(m_settings.maxStepsPerFrame);
            m_droppedGameSeconds += over * step;
            m_accumulator -= over * step;
            count = static_cast<double>(m_settings.maxStepsPerFrame);
        }
        m_accumulator -= count * step;
        if (m_accumulator < 0.0)
        {
            m_accumulator = 0.0;    // 上の「わずかな余裕」で、ほんの少しマイナスになった分
        }
        const int steps = static_cast<int>(count);
        m_totalSteps += static_cast<uint64_t>(steps);
        return steps;
    }

    // 貯まりの残りが、1回の刻みの何割か(0以上1未満)。描画で、前のステップとの間を補間する時に使う。
    double Alpha() const
    {
        const double alpha = m_accumulator / m_settings.stepGameSeconds;
        return alpha < 0.0 ? 0.0 : (alpha >= 1.0 ? 0.999999 : alpha);
    }

    // 貯まりを捨てる(街を読み込んだ時など。前の街の端数を、新しい街に持ち込まない)。
    void Reset() { m_accumulator = 0.0; }

    const FixedTimestepSettings& Settings() const { return m_settings; }
    double StepGameSeconds() const { return m_settings.stepGameSeconds; }
    double AccumulatedGameSeconds() const { return m_accumulator; }     // 今の貯まり(ゲーム内の秒)
    uint64_t TotalSteps() const { return m_totalSteps; }                // これまでに出したステップの回数
    double DroppedGameSeconds() const { return m_droppedGameSeconds; }  // 上限で捨てたゲーム内の時間の合計(その分、遅くなった)

private:
    FixedTimestepSettings m_settings;
    double m_accumulator = 0.0;         // 貯まったゲーム内の時間(秒)。double にして、長時間でも誤差を貯めない
    uint64_t m_totalSteps = 0;
    double m_droppedGameSeconds = 0.0;
};

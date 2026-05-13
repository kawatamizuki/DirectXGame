#include "TimeManager.h"

#include <thread>

TimeManager::TimeManager()
    : m_previousTime(Clock::now())
    , m_deltaTime(0.0f)
    , m_fps(0.0f)
    , m_frameCount(0)
    , m_fpsTimer(0.0f)
    , m_targetFPS(60)
{
}

TimeManager::~TimeManager()
{
}
void TimeManager::BeginFrame()
{
    //========================================
    // フレーム開始時間記録
    //========================================
    //
    // FPS制限用の基準時間を保存する。
    // WaitForTargetFPS() では、この時間から現在までの
    // 経過時間を見て、目標FPSに到達するまで待機する。
    //
    // DeltaTime計測用の m_previousTime とは用途が違うため、
    // m_frameStartTime として別管理している。
    m_frameStartTime = Clock::now();
}

//========================================
// DeltaTime計測
//========================================
//
// 前フレーム終了時刻から現在時刻までの
// 経過時間を計測する。
//
// この値は:
//
// - 移動
// - アニメーション
// - カメラ
// - 物理
//
// など、FPSに依存しない処理で使用する。
//
// 例:
//
// position += speed * deltaTime;
//
// FPS制限とは別用途のため、
// BeginFrame() の時間とは分離している。

void TimeManager::Update()
{
    //========================================
    // 現在時間取得
    //========================================

    auto currentTime = Clock::now();

    //========================================
    // 前フレームからの経過時間計算
    //========================================

    std::chrono::duration<float> elapsed =
        currentTime - m_previousTime;

    m_deltaTime = elapsed.count();

    //========================================
    // FPS計測
    //========================================


    ++m_frameCount;

    m_fpsTimer += m_deltaTime;

    if (m_fpsTimer >= 1.0f)
    {
        m_fps =
            static_cast<float>(m_frameCount) / m_fpsTimer;

        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }

    m_previousTime = currentTime;
}

float TimeManager::GetDeltaTime() const
{
    return m_deltaTime;
}

float TimeManager::GetFPS() const
{
    return m_fps;
}

void TimeManager::SetTargetFPS(int fps)
{
    m_targetFPS = fps;
}

//========================================
// FPS制限
//========================================
//
// BeginFrame() で保存したフレーム開始時間から
// 現在までの経過時間を測定し、
//
// 目標FPSに必要なフレーム時間へ到達するまで待機する。
//
// 60FPSの場合:
//
// 1 / 60 = 約16.6ms
//
// フレーム処理が16.6ms未満で終わった場合、
// 残り時間を待機することでFPSを固定する。
//
// VSync OFF時のみ使用する想定。

void TimeManager::WaitForTargetFPS()
{
    if (m_targetFPS <= 0)
    {
        return;
    }

    float targetFrameTime =
        1.0f / static_cast<float>(m_targetFPS);
    while (true)
    {
        auto now = Clock::now();

        std::chrono::duration<float> elapsed =
            now - m_frameStartTime;

        if (elapsed.count() >= targetFrameTime)
        {
            break;
        }
    }

   
}
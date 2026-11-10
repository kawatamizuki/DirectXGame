#include "TestMapSession.h"

bool TestMapSession::Load(const std::string& id, std::string& message)
{
    // 最初の読み込みの時だけ、今の街を控える(既に控えがあれば、それが「元の街」のまま)。
    std::string captured;
    bool capturedNow = false;
    if (!m_hasOriginal)
    {
        if (!m_io.capture)
        {
            message = "読み込めません: 街の入出力が設定されていません";
            return false;
        }
        std::string captureMessage;
        if (!m_io.capture(captured, captureMessage))
        {
            message = "読み込めません: 今の街を控えられませんでした: " + captureMessage;
            return false;
        }
        capturedNow = true;
    }

    if (!m_store.Load(id, message))
    {
        return false;     // 失敗した時は、街が壊れない(読み込みは、検証してから入れ替える)。控えも、作らない。
    }
    if (capturedNow)
    {
        m_original = std::move(captured);
        m_hasOriginal = true;
    }
    return true;
}

bool TestMapSession::RestoreOriginal(std::string& message)
{
    if (!m_hasOriginal)
    {
        message = "戻せません: 読み込む前の街の控えがありません";
        return false;
    }
    if (!m_io.loadBytes)
    {
        message = "戻せません: 街の入出力が設定されていません";
        return false;
    }
    if (!m_io.loadBytes(m_original, "元の街", message))
    {
        return false;     // 戻せなかった時は、控えを残す(もう一度、試せる)
    }
    m_store.ClearActive();
    ForgetOriginal();
    message = "テストマップを読み込む前の街へ戻しました";
    return true;
}

bool TestMapSession::IsLayoutModified(bool& modified) const
{
    modified = false;
    const TestMapStore::ActiveMap& active = m_store.Active();
    if (!active.valid || !m_io.capture)
    {
        return false;
    }
    std::string bytes, message;
    std::uint64_t layout = 0;
    if (!m_io.capture(bytes, message) || !TestMapStore::ComputeLayoutFingerprint(bytes, layout))
    {
        return false;
    }
    modified = (layout != active.layoutFingerprint);
    return true;
}

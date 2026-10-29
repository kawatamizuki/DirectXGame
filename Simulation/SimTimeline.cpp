#include "SimTimeline.h"
#include <algorithm>

SimTimelineEvents BuildTimeline(const SimDebugRecorder& recorder, uint32_t residentId, const std::function<bool(const SimEvent&)>& pass)
{
    SimTimelineEvents timeline;
    for (const SimEvent& event : recorder.Events())
    {
        if (event.residentId != residentId)
        {
            continue;
        }
        if (pass && !pass(event))
        {
            continue;
        }
        timeline.push_back(&event);
    }
    return timeline;
}

int FindTimelineIndex(const SimTimelineEvents& timeline, uint64_t eventId, bool* clampedToOldest)
{
    if (clampedToOldest)
    {
        *clampedToOldest = false;
    }
    if (timeline.empty())
    {
        return -1;
    }
    // eventId以上の、最初のイベント(古い順に並んでいる)。
    const auto it = std::lower_bound(timeline.begin(), timeline.end(), eventId,
        [](const SimEvent* event, uint64_t id) { return event->eventId < id; });
    if (it != timeline.end() && (*it)->eventId == eventId)
    {
        return static_cast<int>(it - timeline.begin());
    }
    if (it == timeline.begin())
    {
        if (clampedToOldest)
        {
            *clampedToOldest = true; // 先頭より古い
        }
        return 0;
    }
    return static_cast<int>(it - timeline.begin()) - 1; // それより前で、一番近いイベント
}

int PreviousOutingIndex(const SimTimelineEvents& timeline, int index)
{
    if (index <= 0 || index >= static_cast<int>(timeline.size()))
    {
        return -1;
    }
    const uint32_t current = timeline[static_cast<size_t>(index)]->outingId;
    // 今の外出のまとまりを抜ける(外出の番号が変わる所まで戻る)。
    int i = index;
    while (i >= 0 && timeline[static_cast<size_t>(i)]->outingId == current) { --i; }
    if (i < 0)
    {
        return -1;
    }
    // 1つ前の外出の、最初のイベント。
    const uint32_t previous = timeline[static_cast<size_t>(i)]->outingId;
    while (i > 0 && timeline[static_cast<size_t>(i - 1)]->outingId == previous) { --i; }
    return i;
}

int NextOutingIndex(const SimTimelineEvents& timeline, int index)
{
    const int count = static_cast<int>(timeline.size());
    if (index < 0 || index >= count)
    {
        return -1;
    }
    const uint32_t current = timeline[static_cast<size_t>(index)]->outingId;
    int i = index;
    while (i < count && timeline[static_cast<size_t>(i)]->outingId == current) { ++i; }
    return i < count ? i : -1;
}

int PreviousMatchIndex(const SimTimelineEvents& timeline, int index, const std::function<bool(const SimEvent&)>& match)
{
    for (int i = (std::min)(index, static_cast<int>(timeline.size())) - 1; i >= 0; --i)
    {
        if (match(*timeline[static_cast<size_t>(i)])) { return i; }
    }
    return -1;
}

int NextMatchIndex(const SimTimelineEvents& timeline, int index, const std::function<bool(const SimEvent&)>& match)
{
    for (int i = (std::max)(index, -1) + 1; i < static_cast<int>(timeline.size()); ++i)
    {
        if (match(*timeline[static_cast<size_t>(i)])) { return i; }
    }
    return -1;
}

#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "SimDebugRecorder.h"

// 住民1人の、記録されたイベントを「時間軸」として読むための、小さな計算(段階2。履歴のシークバーの土台)。
//  ・時間軸は、記録された「イベント」の並び(古い順)。イベントの無い時刻は、補間しない(正確だったように見せない)。
//  ・これは「記録の閲覧位置」を扱うだけで、街の状態は、巻き戻さない。
//  ・画面(ImGui)に依存しない。単体テストできる。

using SimTimelineEvents = std::vector<const SimEvent*>;

// residentId の、pass が true を返すイベントを、古い順に集める。pass が空なら、全てのイベント。
// 返すポインタは、次に記録を追加する(AddEvent)か、容量の上限で捨てるまで有効(同じフレームの中で使うこと)。
SimTimelineEvents BuildTimeline(const SimDebugRecorder& recorder, uint32_t residentId, const std::function<bool(const SimEvent&)>& pass);

// カーソル(eventId)の位置を、時間軸の添字で返す。
//  ・eventId のイベントが時間軸にあれば、その添字。
//  ・無ければ(絞り込みで外れた・捨てられた)、それより前で一番近いイベント。それより前が無ければ0。
//  ・時間軸が空なら-1。
// clampedToOldest: eventId が、時間軸の先頭より古く、先頭に寄せた時にtrue(古い記録が、捨てられたか、絞り込まれた)。
int FindTimelineIndex(const SimTimelineEvents& timeline, uint64_t eventId, bool* clampedToOldest = nullptr);

// 前の外出(今の外出の1つ前のまとまりの、最初のイベント)の添字。無ければ-1。
// 今のイベントが外出の途中なら、「今の外出の最初」へ(1回目の操作)ではなく、必ず「1つ前の外出」の最初へ行く。
int PreviousOutingIndex(const SimTimelineEvents& timeline, int index);

// 次の外出の、最初のイベントの添字。無ければ-1。
int NextOutingIndex(const SimTimelineEvents& timeline, int index);

// index より前(後)で、match が true を返す、一番近いイベントの添字。無ければ-1。
int PreviousMatchIndex(const SimTimelineEvents& timeline, int index, const std::function<bool(const SimEvent&)>& match);
int NextMatchIndex(const SimTimelineEvents& timeline, int index, const std::function<bool(const SimEvent&)>& match);

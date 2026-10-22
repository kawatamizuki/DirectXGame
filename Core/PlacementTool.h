#pragma once

// Playingモードで「今何を配置しようとしているか」を表す。
// BuildController(建物)とRoadSystem(道路)は同じ左クリック(InputAction::Decide)を取り合うため、
// どちらのシステムがクリックを処理するかをこのフラグで切り替える。
enum class PlacementTool
{
    None,       // 何も配置しない(ゲーム内メニューで建物・道路を選ぶまでの状態。クリックしても何も置かれない)
    Building,
    Road
};

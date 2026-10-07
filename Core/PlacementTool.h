#pragma once

// Playingモードで「今何を配置しようとしているか」を表す。
// BuildController(建物)とRoadSystem(道路)は同じ左クリック(InputAction::Decide)を取り合うため、
// どちらのシステムがクリックを処理するかをこのフラグで切り替える。
enum class PlacementTool
{
    Building,
    Road
};

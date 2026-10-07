#pragma once
#include <string>
#include <vector>
#include "Json.h"
#include "WorldSnapshot.h"

// WorldSnapshot(今の街の状態のコピー)と、形式に依存しない値の木(Json)を相互に変換する。
// ファイルに書く形(JSONテキストやバイナリ)はISnapshotFormatの担当で、ここは知らない。
// 道路の種類・建物の種類などの列挙は、数値ではなく**名前**で保存する(列挙の並びを変えても
// 古いセーブが読めるように)。表示名(GetBuildingDefinition().nameなど)とは独立した、
// 保存専用の安定した名前を使う。
namespace ScenarioFile
{
    // snapshotを値の木にする(一番外側はオブジェクト)。
    Json SnapshotToJson(const WorldSnapshot& snapshot);

    // 値の木をsnapshotに読み込む。成功したらtrue。
    //  - 知らない項目・知らない状態区画は無視する(warningsに記録するだけ)。
    //  - 足りない項目は既定値になる。
    //  - 版が新しすぎる/必須の項目が壊れている場合はfalseでerrorに理由を入れる(クラッシュしない)。
    // snapshotは成功時だけ書き換えられる(失敗した時に中途半端な内容が残らない)。
    bool JsonToSnapshot(const Json& root, WorldSnapshot& snapshot, std::vector<std::string>& warnings, std::string& error);

    // 保存専用の安定した名前との変換。変換できない名前の時はfalse。
    const char* RoadTypeKey(RoadType type);
    bool RoadTypeFromKey(const std::string& key, RoadType& out);
    const char* RoadDrawModeKey(RoadDrawMode mode);
    bool RoadDrawModeFromKey(const std::string& key, RoadDrawMode& out);
    const char* BuildingTypeKey(BuildingType type);
    bool BuildingTypeFromKey(const std::string& key, BuildingType& out);
}

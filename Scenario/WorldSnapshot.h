#pragma once
#include <vector>
#include <DirectXMath.h>
#include "RoadSegment.h"
#include "BuildingType.h"
#include "BuildingPlacement.h"
#include "Transform.h"
#include "Json.h"

// セーブする建物1棟ぶん(自己完結したレコード)。
struct SnapshotBuilding
{
    BuildingType type = BuildingType::House;
    Transform transform;          // 位置・回転・拡大(見た目)
    BuildingPlacement placement;  // 配置の確定結果(占有範囲・基準座標系・ローカルマス)
};

// セーブする青マス1つぶん。GridOrientationRegistryは「先に作られた方が優先」なので、
// 一覧は作られた順(=優先順)のまま保存する。
struct SnapshotOrientationCell
{
    DirectX::XMFLOAT3 origin{};
    float yaw = 0.0f;
    int cellX = 0;
    int cellZ = 0;
};

// 「今の街の状態」をそのままコピーしたもの(メモリ上の中間表現)。
// 道路・建物・青マスは、操作の履歴ではなく現在の状態を持つ。決まった手順で作り直せる派生データ
// (道路ノード・占有・道路メッシュ・住民の見た目)は保存せず、読み込み後に作り直す。
// システムごとの状態(時計・フラグ・住民など)はstateの区画に入る(ISaveable)。
struct WorldSnapshot
{
    // 今のセーブデータの版。形式を変えたら上げ、古い版を読む変換(ScenarioFile)を足す。
    static constexpr int kCurrentVersion = 1;

    int version = kCurrentVersion;

    std::vector<RoadSegment> roads;                          // debugLoad(混雑の表示用)は保存しない
    std::vector<SnapshotBuilding> buildings;                 // 配列の添字がそのまま建物のuid(他の保存データが建物を指す時に使う)
    std::vector<SnapshotOrientationCell> orientationCells;   // 作られた順(=優先順)

    // システムごとの状態の区画。キーがISaveable::SaveKey()。各区画には"version"が入る。
    Json state = Json::MakeObject();
};

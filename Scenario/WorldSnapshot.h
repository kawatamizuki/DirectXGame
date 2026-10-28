#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "RoadSegment.h"
#include "BuildingType.h"
#include "BuildingPlacement.h"
#include "Transform.h"
#include "Json.h"
#include "WorldScale.h"

// セーブする建物1棟ぶん(自己完結したレコード)。
struct SnapshotBuilding
{
    BuildingType type = BuildingType::House;
    Transform transform;          // 位置・回転・拡大(見た目)
    BuildingPlacement placement;  // 配置の確定結果(占有範囲・基準座標系・ローカルマス)
};

// セーブする飾り(Prop)1つぶん。モデルは「プロジェクト相対のファイルパス」で持ち、読み込み時に同じモデルを用意する。
struct SnapshotProp
{
    std::string modelPath;  // 区切りは「/」。絶対パスは持たない(提出物に個人のフォルダ名が混ざらないように)
    Transform transform;    // 位置・回転・拡大
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

    // 保存した時の1マスの大きさ(ワールド単位)。座標・大きさ・速さは、この大きさを基準にした値。
    // 読み込む時、今のマスの大きさとの比で変換する(ScaleWorldSnapshot)。記録が無い古いセーブは、当時の大きさ(kLegacyCellSize)。
    static constexpr float kLegacyCellSize = WorldScale::kLegacyCellSize;
    float cellSize = kLegacyCellSize;

    // 保存した時の「長さの基準」(WorldScale::kLengthUnit)。道路の座標・幅・建物の位置と大きさ・住民の速さは、この大きさを基準にした値。
    // マスの細かさ(cellSize)とは別。マスを細かく分割しても、世界の大きさは変わらない。
    // 0 = 記録が無い(長さの基準を記録する前の古いセーブ)。その頃は、マスと長さの基準が同じ大きさだったので、cellSizeと同じとして扱う(GetLengthUnit)。
    float lengthUnit = 0.0f;
    float GetLengthUnit() const { return lengthUnit > 0.0f ? lengthUnit : cellSize; }

    std::vector<RoadSegment> roads;                          // debugLoad(混雑の表示用)は保存しない
    std::vector<SnapshotBuilding> buildings;                 // 配列の添字がそのまま建物のuid(他の保存データが建物を指す時に使う)
    std::vector<SnapshotOrientationCell> orientationCells;   // 作られた順(=優先順)
    std::vector<SnapshotProp> props;                         // 飾り。無い(古いセーブ)なら空

    // システムごとの状態の区画。キーがISaveable::SaveKey()。各区画には"version"が入る。
    Json state = Json::MakeObject();
};

// snapshotの、場所・大きさに関する値(道路の座標と幅、建物と飾りの位置と拡大、建物の占有範囲、青マスの原点)を、
// factor倍する。マスの大きさを変えた時に、古いセーブの街を、今のマスの大きさに合わせて読み込むために使う。
// 「何マス目か」の整数(マスの番号・マスの数)と、向き(角度)は変わらない。
// システムごとの状態(state。住民の位置・速さなど)は、各システムが自分でLoadContext::spaceScaleを使って変換する。
void ScaleWorldSnapshot(WorldSnapshot& snapshot, float factor);

// snapshotの、マス番号・マスの数を、マスをfactor分割した(1マスが縦横factor個に分かれた)格子に変換する。
// 建物の占有マス(開始番号と幅・奥行きのマス数)はfactor倍、青マスは1つがfactor x factor個に分かれる(優先順は保つ)。
// 座標・大きさ・向きは変わらない(同じ場所を、細かい格子で数え直すだけ)。factor<=1なら何もしない。
// 呼ぶ前に、cellSizeはまだ元のままで、呼んだ後にcellSizeをfactorで割る。
void RefineSnapshotGrid(WorldSnapshot& snapshot, int factor);

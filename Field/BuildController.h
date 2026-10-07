#pragma once
#include <vector>
#include <DirectXMath.h>
#include "GridCoord.h"
#include "OrientedRectangleOverlap.h"
#include "BuildingType.h"
#include "Transform.h"

struct GameContext;
class Field;
class IOccupancyGrid;
class Model;
class RoadSystem;
class GridOrientationRegistry;

// フィールド上への建物配置コントローラー(Playingモード時のみ動作)。
class BuildController
{
public:
    void Initialize(
        GameContext* context, Field* field, IOccupancyGrid* occupancy,
        RoadSystem* roadSystem, GridOrientationRegistry* orientationRegistry,
        Model* houseModel, Model* officeModel, Model* shopModel);

    void Update();
    void Draw();

private:
    // 建物配置の基準となるローカル座標系(原点・角度)。
    // 近くの道路/GridOrientationRegistryに角度が既に決まっていればそれに合わせ、
    // 無ければワールド軸のまま(yaw=0, origin=(0,0,0))。yaw=0の時は今までの
    // ワールド軸グリッドと完全に同じ計算結果になる。
    struct PlacementFrame
    {
        DirectX::XMFLOAT3 origin{};
        float yaw = 0.0f;
    };

    // ComputePlacementの計算結果。確定配置(Update)とゴーストプレビュー(Draw)の
    // 両方がこれを共有することで、見た目と実際の配置結果がズレないようにする。
    struct PlacementPreview
    {
        Transform transform;                    // モデルをこのまま描画/生成すれば良い最終Transform
        std::vector<GridCoord> footprintCells;   // 触れる全ワールドセル(回転を反映済み。範囲チェックと道路側の保守的な占有判定用)
        OrientedRect footprintRect;              // footprintの実際の形(ワールド座標。空き判定と占有登録に使う)
        GridCoord localCell;                     // frameのローカル座標系での、footprintの最小角のマス番号
        int localWidthCells = 1;                 // 回転(90度刻み)を反映した、ローカルマス数での幅
        int localDepthCells = 1;                 // 同じく奥行き
        bool isValid = false;                    // 範囲内かつ空いているか(falseなら配置不可)
        PlacementFrame frame;                    // 実際に使った基準座標系(確定時にマスを登録するのに使う)
    };

    // typeに対応するモデルを返す(表示・配置サイズ計算の両方で使う)。
    Model* GetModelForType(BuildingType type) const;

    // footprint(占有セル範囲)の左下コーナーcellから、widthCells x depthCellsぶんの全セルを列挙する
    // (frameのローカル座標系での話。ワールド軸グリッドではない場合がある)。
    std::vector<GridCoord> GetFootprintCells(const GridCoord& cell, int widthCells, int depthCells) const;

    // hitPointの位置で使うべき配置基準座標系を決める。
    // 優先順位: ①GridOrientationRegistryで既に確定しているチャンクならその角度
    // ②近くに道路があればその向き ③どちらも無ければワールド軸(yaw=0)。
    PlacementFrame ComputePlacementFrame(const DirectX::XMFLOAT3& hitPoint) const;

    // frameのローカルセル矩形(cell〜widthCells x depthCells)を、ワールド座標の
    // 向き付き矩形に変換する(空き判定・占有登録・触れるワールドセルの列挙の共通の入力)。
    OrientedRect MakeFootprintRect(
        const GridCoord& cell, int widthCells, int depthCells,
        const PlacementFrame& frame, float cellSize) const;

    // ローカル座標系の点をワールド座標に変換する(frame.origin + frameのyawで回転)。
    DirectX::XMFLOAT3 LocalToWorld(const DirectX::XMFLOAT3& localPoint, const PlacementFrame& frame) const;

    // 現在選択中の建物種別・回転角度・hitPointの位置で、配置した場合のTransform・
    // 占有セル・配置可否をまとめて計算する。Update()の確定配置とDraw()の
    // ゴーストプレビューの両方から呼ばれる、配置計算の唯一の情報源。
    PlacementPreview ComputePlacement(const DirectX::XMFLOAT3& hitPoint) const;

    // 選択中の建物を90度回転する(4ステップで1周)。
    // ImGuiボタン・InputAction::Rotateの両方から呼ばれる(将来ImGuiを置き換えてもロジックは無傷)。
    void RotateSelection();

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
    IOccupancyGrid* m_occupancy = nullptr;
    RoadSystem* m_roadSystem = nullptr;
    GridOrientationRegistry* m_orientationRegistry = nullptr;

    // 建物種別ごとのモデル。
    Model* m_houseModel = nullptr;
    Model* m_officeModel = nullptr;
    Model* m_shopModel = nullptr;

    bool m_hasHoverPoint = false;
    DirectX::XMFLOAT3 m_hoverPoint{}; // 生のカーソル着地点(ワールド座標)

    // 今から配置する建物の種類。
    // "Build"ウィンドウは本格的なin-game UIに置き換える前提の仮のImGuiボタンで選択する。
    BuildingType m_selectedType = BuildingType::House;

    // 今から配置する建物の向き(0〜3、1ステップ=基準角度からのY軸90度)。
    int m_selectedRotationStep = 0;

    // GridOrientationRegistryで確定済みのチャンク境界・基準角度を可視化するデバッグ表示のON/OFF
    // (Buildウィンドウのチェックボックスで切り替える。既定OFF)。
    bool m_showOrientationDebug = false;
};

#pragma once
#include <windows.h>
#include <d3d11.h>

#include"GameContext.h"
#include"Ray.h"
enum class DragMoveMode
{
    CameraPlane,
    XZPlane,
    XYPlane,
    YZPlane
};


enum class GizmoAxis
{
    None,
    X,
    Y,
    Z
};

enum class GizmoSpace
{
    World,
    Local
};

enum class GizmoMode
{
    Move,
    Rotate,
    Scale
};


class DebugEditor
{
public:
    DebugEditor();
    ~DebugEditor();
    bool Initialize(HWND hwnd, GameContext* context);
    void BeginFrame();
    void Update();
    void UpdatePicking();//オブジェクトを選択するための関数
    void UpdateDragging();//MoveModeでドラッグして動かせるようにする関数
    void UpdateRotateGizmoDrag();//RotateModeでドラッグして回転させる関数
    void UpdateGizmoHover();//どの軸を触っているかをかを判定する関数
    void UpdateMoveGizmoHover();//移動軸のどれを触っているか判定する関数
    void UpdateRotateGizmoHover();//回転軸のどれを触っているかを判定する関数
    void UpdateFocusSelected();//選択しているオブジェクトにカメラを向ける関数
    void UpdateGizmoMode();//move,rotate,scaleの切り替え

    void Draw();
    void DrawAllObjectBounds();//すべての当たり判定のBOX描画
    void DrawSelectedObjectBounds();//選択しているオブジェクトの当たり判定のBOXの描画
    void DrawPerformance();//パフォーマンス設定の描画
    void DrawObjects();//オブジェクト一覧
    void DrawInspector();//オブジェクトの座標やスケールの表示
    void DrawEditorSettings();//エディターに関する設定
    void DrawMoveGizmo();//軸の表示
    void DrawRotateGizmo();
    void EndFrame();

    Ray CreateMouseRay();

    bool WorldToScreen(const DirectX::XMFLOAT3& worldPos,DirectX::XMFLOAT2& screenPos);//3D座標を画面上の2D座標に変換する関数
    bool IntersectRayPlane( const Ray& ray,const DirectX::XMFLOAT3& planePoint,const DirectX::XMFLOAT3& planeNormal,DirectX::XMFLOAT3& hitPoint);
    float DistanceRayToSegment( const Ray& ray,const DirectX::XMFLOAT3& segStart,const DirectX::XMFLOAT3& segEnd);
    float DistancePointToSegment2D(const DirectX::XMFLOAT2& point, const DirectX::XMFLOAT2& segStart, const DirectX::XMFLOAT2& segEnd);//2D上のスクリーンとGizmoを判定
    DirectX::XMFLOAT3 GetAxisDirection(GizmoAxis axis, const GameObject& obj);//軸方向とオブジェクトを返す関数

    //カメラ系の関数
    void UpdateFreeCamera();
  

    void Finalize();
private:
    GameContext* m_context = nullptr;
    //BOXの描画フラグ
    bool m_showSelectedBounds = false;
    bool m_showAllBounds = false;

    bool m_isDraggingObject = false;//今ドラッグしているか
    bool m_enableObjectDragging = true;//ドラッグ操作を許可するか
    DragMoveMode m_dragMoveMode = DragMoveMode::CameraPlane;//ドラッグモードの切替
    GizmoAxis m_hoveredAxis = GizmoAxis::None;//オブジェクトの横に表示されている軸の判定
    GizmoAxis m_activeAxis = GizmoAxis::None;//今選択されている軸の判定
    GizmoSpace m_gizmoSpace = GizmoSpace::World;//ワールド座標かローカル座標か
    GizmoMode m_gizmoMode = GizmoMode::Move;//何を変更するか
    bool m_isDraggingGizmo = false;//ドラッグしているか
    //MoveModeのドラッグ用変数
 
    POINT m_axisDragStartMousePos = { 0, 0 };//軸移動をマウスの動きにするため
    DirectX::XMFLOAT3 m_axisDragStartObjectPos = { 0, 0, 0 };
 

    //RotateModeのドラッグ用変数
    POINT m_rotateDragStartMousePos = { 0, 0 };
    DirectX::XMFLOAT3 m_rotateDragStartRotation = { 0, 0, 0 };

    // RotateGizmo ドラッグ用
    DirectX::XMFLOAT3 m_rotateDragStartVector = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotateDragAxis = { 0.0f, 1.0f, 0.0f };

    float m_rotateDragSensitivity = 0.01f;

    int m_dragObjectIndex = -1;
    DirectX::XMFLOAT3 m_dragOffset = { 0, 0, 0 };
    int m_selectedObjectIndex;

    //カメラ用変数
    POINT m_prevMousePos = { 0, 0 };
    bool m_isFreeCameraActive = false;
    float m_freeCameraMoveSpeed = 0.2f;
    float m_freeCameraRotateSpeed = 0.005f;
};
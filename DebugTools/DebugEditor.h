#pragma once
#include <windows.h>
#include <d3d11.h>
#include <vector>

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
    Z,
    Center
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

struct TransformCommand
{
    int objectIndex = -1;
    Transform before;
    Transform after;
};

enum class EditorViewMode
{
    SceneView,
    GameView
};

struct EditorRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
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
    void UpdateScaleGizmoDrag();//ScaleModeでドラッグしてスケールを変更する関数
    void UpdateRotateGizmoDrag();//RotateModeでドラッグして回転させる関数
    void UpdateGizmoHover();//どの軸を触っているかをかを判定する関数
    void UpdateMoveGizmoHover();//移動軸のどれを触っているか判定する関数
    void UpdateScaleGizmoHover();//スケール軸のどれを触っているか判定する関数    
    void UpdateRotateGizmoHover();//回転軸のどれを触っているかを判定する関数
    void EndGizmoDragIfNeeded();//共通の終了判定
    void UpdateFocusSelected();//選択しているオブジェクトにカメラを向ける関数
    void UpdateGizmoMode();//move,rotate,scaleの切り替え

    void Draw();
    void DrawAllObjectBounds();//すべての当たり判定のBOX描画
    void DrawSelectedObjectBounds();//選択しているオブジェクトの当たり判定のBOXの描画
    void DrawPerformance();//パフォーマンス設定の描画
    void DrawObjects();//オブジェクト一覧
    void DrawInspector();//オブジェクトの座標やスケールの表示
    void DrawEditorSettings();//エディターに関する設定
    void DrawHierarchyView();// Unity風のHierarchy表示
    void DrawInspectorView();// Unity風のInspector表示
    void DrawEditorSettingsView();// Editor設定表示
    void DrawDebugView();// Debug / Performance表示
    void DrawMoveGizmo();//移動軸の表示
    void DrawScaleGizmo();//スケール軸の表示
	void DrawRotateGizmo();//回転軸の表示
    void DrawMoveAxisArrow(const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT3& dir, const DirectX::XMFLOAT4& color, float gizmoLength );//移動軸に矢印描画
    void DrawScaleAxisBox(const DirectX::XMFLOAT3& end,const DirectX::XMFLOAT4& color,float boxSize);
    void DrawRotateRing( const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& axis, float radius, const DirectX::XMFLOAT4& color);
    void EndFrame();

    Ray CreateMouseRay();

    bool WorldToScreen(const DirectX::XMFLOAT3& worldPos,DirectX::XMFLOAT2& screenPos);//3D座標を画面上の2D座標に変換する関数
    bool IntersectRayPlane( const Ray& ray,const DirectX::XMFLOAT3& planePoint,const DirectX::XMFLOAT3& planeNormal,DirectX::XMFLOAT3& hitPoint);
    float DistanceRayToSegment( const Ray& ray,const DirectX::XMFLOAT3& segStart,const DirectX::XMFLOAT3& segEnd);
    float DistancePointToSegment2D(const DirectX::XMFLOAT2& point, const DirectX::XMFLOAT2& segStart, const DirectX::XMFLOAT2& segEnd);//2D上のスクリーンとGizmoを判定
    DirectX::XMFLOAT3 GetAxisDirection(GizmoAxis axis, const GameObject& obj);//軸方向とオブジェクトを返す関数

    //カメラ系の関数
    void UpdateFreeCamera();
  

    // 選択・ドラッグ操作を解除する(セーブデータの読み込みでオブジェクトが入れ替わる時に呼ぶ。
    // 選択は配列の添字で持っているため、そのままだと別のオブジェクトを指してしまう)。
    void ClearSelection();

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
    // ScaleModeのドラッグ用
    DirectX::XMFLOAT3 m_scaleDragStartScale = { 1.0f, 1.0f, 1.0f};
    //RotateModeのドラッグ用変数
    POINT m_rotateDragStartMousePos = { 0, 0 };
    DirectX::XMFLOAT4 m_rotateDragStartQuat = { 0.0f,0.0f, 0.0f,1.0f };

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

	// Undo/Redo用スタック
    std::vector<TransformCommand> m_undoStack;
    std::vector<TransformCommand> m_redoStack;

    Transform m_dragStartTransform;

    static constexpr size_t MaxUndoCount = 200;

    void PushTransformCommand(int objectIndex,const Transform& before,const Transform& after);

    void Undo();
    void Redo();
    
    static bool NearlyEqual( float a,float b,float epsilon = 0.0001f);

    static bool NearlyEqual(const DirectX::XMFLOAT3& a,const DirectX::XMFLOAT3& b,float epsilon = 0.0001f);

    static bool NearlyEqual(const DirectX::XMFLOAT4& a,const DirectX::XMFLOAT4& b,float epsilon = 0.0001f);

    static bool IsSameTransform(const Transform& a,const Transform& b);
};
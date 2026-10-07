#include <DirectXMath.h>
#include <algorithm>
#include <cfloat>
#include <algorithm>
#include "DebugEditor.h"
#include"Renderer.h"
#include"DebugRenderer.h"
#include"TimeManager.h"
#include"InputManager.h"
#include"Collision.h"
#include"Ray.h"
#include"BuildingType.h"
#include"DemandSystem.h"



#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

DebugEditor::DebugEditor()
    : m_context(nullptr)
    , m_selectedObjectIndex(-1)
{
}

DebugEditor::~DebugEditor()
{
}

bool DebugEditor::Initialize(HWND hwnd, GameContext* context)
{
    if (!hwnd || !context || !context->renderer)
    {
        return false;
    }
  
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // 日本語のログ/UI文字列(Debug::LogやRoadSystemの確認ボタンなど)を正しく表示するため、
    // Windows標準搭載のメイリオフォントを日本語グリフ範囲付きで読み込む
    // (フォントファイルを同梱していないため実行環境のフォントを使う)。
    // 読み込みに失敗した場合(フォントが無い環境など)はImGui標準フォント(ASCIIのみ)にフォールバックする。
    ImGuiIO& io = ImGui::GetIO();
    ImFont* japaneseFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\meiryo.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    if (!japaneseFont)
    {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX11_Init(
        context->renderer->GetDevice(),
        context->renderer->GetContext()
    );

    m_context = context;


    return true;
}


void DebugEditor::UpdatePicking()
{
    if (m_isDraggingGizmo)
    {
        return;
    }

    if (m_isDraggingObject)
    {
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    if (m_hoveredAxis != GizmoAxis::None)
    {
        return;
    }

    if (!m_context->input->IsActionPressed(InputAction::Decide))
    {
        return;
    }

    Ray ray = CreateMouseRay();

    float nearestDistance = FLT_MAX;
    int selectedIndex = -1;

    for (int i = 0; i < m_context->objects->size(); ++i)
    {
        GameObject& obj =
            (*m_context->objects)[i];
        float hitDistance = 0.0f;

        if (!obj.model)
        {
            continue;
        }

        if (IntersectRayOBB(
            ray,
            obj.transform,
            obj.model->GetBoundsMin(),
            obj.model->GetBoundsMax(),
            hitDistance))
        {
            if (hitDistance < nearestDistance)
            {
                nearestDistance = hitDistance;
                selectedIndex = i;
            }
        }
    }

    for (int i = 0; i < m_context->objects->size(); ++i)
    {
        GameObject& obj =
            (*m_context->objects)[i];

    
    }

    m_selectedObjectIndex = selectedIndex;
}

// 選択中Objectをドラッグ移動する
void DebugEditor::UpdateDragging()
{
    // Context不正なら終了
    if (!m_context ||
        !m_context->objects ||
        !m_context->camera ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    if (!m_enableObjectDragging)
    {
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse &&
        !m_isDraggingObject &&
        !m_isDraggingGizmo)
    {
        return;
    }

    // 選択中Object取得
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    // =========================
    // Gizmo軸ドラッグ開始
    // =========================
    if (!m_isDraggingGizmo &&
        m_hoveredAxis != GizmoAxis::None &&
        m_context->input->IsActionPressed(InputAction::Decide))
    {
        m_dragStartTransform =
            obj.transform;

        m_isDraggingGizmo = true;
        m_activeAxis = m_hoveredAxis;
        m_dragObjectIndex = m_selectedObjectIndex;

        m_axisDragStartObjectPos =
            obj.transform.position;

        GetCursorPos(&m_axisDragStartMousePos);
        ScreenToClient(GetActiveWindow(), &m_axisDragStartMousePos);

        return;
    }

    // =========================
    // Gizmo軸ドラッグ中
    // =========================
    if (m_isDraggingGizmo &&
        m_context->input->IsActionDown(InputAction::Decide))
    {
        // =========================
        // 現在マウス座標取得
        // =========================
        POINT currentMousePos;

        GetCursorPos(&currentMousePos);

        ScreenToClient(
            GetActiveWindow(),
            &currentMousePos
        );

        // =========================
        // ドラッグ開始時からの
        // マウス移動量
        // =========================
        DirectX::XMFLOAT2 mouseDelta =
        {
            static_cast<float>(
                currentMousePos.x -
                m_axisDragStartMousePos.x
            ),

            static_cast<float>(
                currentMousePos.y -
                m_axisDragStartMousePos.y
            )
        };

        // =========================
        // 現在ドラッグ中軸方向
        // =========================
        DirectX::XMFLOAT3 axisDir =
            GetAxisDirection(m_activeAxis, obj);

        using namespace DirectX;

        // =========================
        // ドラッグ開始時Object位置
        // =========================
        XMFLOAT3 startPos =
            m_axisDragStartObjectPos;

        // =========================
        // 3D軸の開始位置
        // =========================
        XMFLOAT3 axisStart =
            startPos;

        // =========================
        // 3D軸の終了位置
        // =========================
        // 軸方向へ1伸ばす
        XMFLOAT3 axisEnd =
        {
            startPos.x + axisDir.x,
            startPos.y + axisDir.y,
            startPos.z + axisDir.z
        };

        // =========================
        // Screen変換用
        // =========================
        XMFLOAT2 axisStartScreen;
        XMFLOAT2 axisEndScreen;

        // =========================
        // 3D軸を画面座標へ変換
        // =========================
        if (WorldToScreen(
            axisStart,
            axisStartScreen) &&

            WorldToScreen(
                axisEnd,
                axisEndScreen))
        {
            // =========================
            // 画面上の軸方向
            // =========================
            XMFLOAT2 screenAxis =
            {
                axisEndScreen.x -
                axisStartScreen.x,

                axisEndScreen.y -
                axisStartScreen.y
            };

            // =========================
            // 長さ計算
            // =========================
            float length =
                sqrtf(
                    screenAxis.x * screenAxis.x +
                    screenAxis.y * screenAxis.y
                );

            // 長さ0防止
            if (length > 0.0001f)
            {
                // =========================
                // 正規化
                // =========================
                screenAxis.x /= length;
                screenAxis.y /= length;

                // =========================
                // マウス移動量を
                // 軸方向へ投影
                // =========================
                float projected =
                    mouseDelta.x * screenAxis.x +
                    mouseDelta.y * screenAxis.y;

                // =========================
                // 移動速度
                // =========================
                float moveScale = 0.02f;

                // =========================
                // 実際の移動量
                // =========================
                float moveAmount =
                    projected * moveScale;

                // =========================
                // 軸方向へ移動
                // =========================
                obj.transform.position.x =
                    m_axisDragStartObjectPos.x +
                    axisDir.x * moveAmount;

                obj.transform.position.y =
                    m_axisDragStartObjectPos.y +
                    axisDir.y * moveAmount;

                obj.transform.position.z =
                    m_axisDragStartObjectPos.z +
                    axisDir.z * moveAmount;
            }
        }

        return;
    }

    // =========================
    // ドラッグ用平面
    // =========================
    // 平面中心
    DirectX::XMFLOAT3 planePoint =
        obj.transform.position;

    // カメラ前方向
    // 画面に対して正面向きの平面になる
    DirectX::XMFLOAT3 planeNormal;

    switch (m_dragMoveMode)
    {
    case DragMoveMode::CameraPlane:
        planeNormal = m_context->camera->GetForward();
        break;

    case DragMoveMode::XZPlane:
        planeNormal = { 0.0f, 1.0f, 0.0f };
        break;

    case DragMoveMode::XYPlane:
        planeNormal = { 0.0f, 0.0f, 1.0f };
        break;

    case DragMoveMode::YZPlane:
        planeNormal = { 1.0f, 0.0f, 0.0f };
        break;
    }

    // =========================
    // ドラッグ開始
    // =========================
    if (m_context->input->IsActionPressed(InputAction::Decide))
    {
        // マウスRay作成
        Ray ray =
            CreateMouseRay();

        DirectX::XMFLOAT3 hitPoint;

        // Rayとドラッグ平面の交点取得
        if (IntersectRayPlane(
            ray,
            planePoint,
            planeNormal,
            hitPoint))
        {
            // ドラッグ開始
            m_isDraggingObject = true;

            m_dragObjectIndex =
                m_selectedObjectIndex;

            // =========================
            // Undo用
            // ドラッグ開始時のTransformを保存
            // =========================
            m_dragStartTransform =
                obj.transform;

            // =========================
            // Object中心との差分保存
            // =========================
            // クリック位置が中心からズレていても
            // 自然にドラッグできる
            m_dragOffset.x =
                obj.transform.position.x - hitPoint.x;

            m_dragOffset.y =
                obj.transform.position.y - hitPoint.y;

            m_dragOffset.z =
                obj.transform.position.z - hitPoint.z;
        }
    }

    // =========================
    // ドラッグ中
    // =========================
    if (m_isDraggingObject &&
        m_context->input->IsActionDown(InputAction::Decide))
    {
        // マウスRay
        Ray ray =
            CreateMouseRay();

        DirectX::XMFLOAT3 hitPoint;

        // Rayと平面交差
        if (IntersectRayPlane(
            ray,
            planePoint,
            planeNormal,
            hitPoint))
        {
            // =========================
            // Object移動
            // =========================
            // ドラッグ開始時のOffsetを維持
            obj.transform.position.x =
                hitPoint.x + m_dragOffset.x;

            obj.transform.position.y =
                hitPoint.y + m_dragOffset.y;

            obj.transform.position.z =
                hitPoint.z + m_dragOffset.z;
        }
    }

    // =========================
    // ドラッグ終了
    // =========================
    if (m_isDraggingObject &&
        !m_context->input->IsActionDown(InputAction::Decide))
    {
        // =========================
        // Undo登録
        // =========================
        PushTransformCommand(
            m_dragObjectIndex,
            m_dragStartTransform,
            obj.transform
        );

        m_isDraggingObject = false;
        m_dragObjectIndex = -1;
    }
}
void DebugEditor::UpdateScaleGizmoDrag()
{
    //========================================================
    // Contextチェック
    //
    // 必要な情報が存在しない場合は処理しない
    //========================================================
    if (!m_context ||
        !m_context->objects ||
        !m_context->input ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    //========================================================
    // 選択中Object取得
    //========================================================
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    //========================================================
    // Modelが無いObjectは操作対象外
    //========================================================
    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    

    //========================================================
    // Scale Gizmoドラッグ開始
    //========================================================
    if (!m_isDraggingGizmo &&
        m_hoveredAxis != GizmoAxis::None &&
        m_context->input->IsActionPressed(InputAction::Decide))
    {
        m_dragStartTransform =obj.transform;
        m_isDraggingGizmo = true;
        m_activeAxis = m_hoveredAxis;
        m_dragObjectIndex = m_selectedObjectIndex;

        //====================================================
        // ドラッグ開始時のScaleを保存
        //
        // ドラッグ中はこの値を基準にしてScale量を計算する
        //====================================================
        m_scaleDragStartScale =
            obj.transform.scale;

        //====================================================
        // ドラッグ開始時のマウス位置保存
        //====================================================
        GetCursorPos(&m_axisDragStartMousePos);

        ScreenToClient(
            GetActiveWindow(),
            &m_axisDragStartMousePos
        );

        return;
    }

    //========================================================
    // Scale Gizmoドラッグ中
    //========================================================
    if (m_isDraggingGizmo &&
        m_context->input->IsActionDown(InputAction::Decide))
    {
        //====================================================
        // 現在のマウス座標取得
        //====================================================
        POINT currentMousePos;

        GetCursorPos(&currentMousePos);

        ScreenToClient(
            GetActiveWindow(),
            &currentMousePos
        );

        //====================================================
        // ドラッグ開始位置からのマウス移動量
        //====================================================
        XMFLOAT2 mouseDelta =
        {
            static_cast<float>(
                currentMousePos.x -
                m_axisDragStartMousePos.x
            ),

            static_cast<float>(
                currentMousePos.y -
                m_axisDragStartMousePos.y
            )
        };

        //====================================================
        // Centerハンドルの場合
        //
        // 特定の3D軸ではなく、
        // 画面上の左右ドラッグ量で比率維持Scaleする
        //====================================================
        if (m_activeAxis == GizmoAxis::Center)
        {
            float scaleSpeed =
                0.01f;

            float scaleAmount =
                mouseDelta.x * scaleSpeed;

            float factor =
                1.0f + scaleAmount;

            factor =
                (std::max)(0.01f, factor);

            obj.transform.scale.x =
                m_scaleDragStartScale.x * factor;

            obj.transform.scale.y =
                m_scaleDragStartScale.y * factor;

            obj.transform.scale.z =
                m_scaleDragStartScale.z * factor;

            //================================================
            // Scale下限
            //================================================
            obj.transform.scale.x =
                (std::max)(0.01f, obj.transform.scale.x);

            obj.transform.scale.y =
                (std::max)(0.01f, obj.transform.scale.y);

            obj.transform.scale.z =
                (std::max)(0.01f, obj.transform.scale.z);

            return;
        }

        //====================================================
        // X/Y/Z軸Scale用の軸方向取得
        //
        // Centerではここを通らない
        //====================================================
        XMFLOAT3 axisDir =
            GetAxisDirection(
                m_activeAxis,
                obj
            );

        //====================================================
        // Gizmo中心
        //====================================================
        XMFLOAT3 origin =
            obj.transform.position;

        //====================================================
        // 軸方向へ1伸ばした位置
        //
        // 画面上での軸方向を求めるために使用
        //====================================================
        XMFLOAT3 axisEnd =
        {
            origin.x + axisDir.x,
            origin.y + axisDir.y,
            origin.z + axisDir.z
        };

        XMFLOAT2 originScreen;
        XMFLOAT2 axisEndScreen;

        //====================================================
        // 3D座標を画面座標へ変換
        //====================================================
        if (WorldToScreen(origin, originScreen) &&
            WorldToScreen(axisEnd, axisEndScreen))
        {
            //================================================
            // 画面上の軸方向ベクトル
            //================================================
            XMFLOAT2 screenAxis =
            {
                axisEndScreen.x - originScreen.x,
                axisEndScreen.y - originScreen.y
            };

            //================================================
            // 軸ベクトル長さ
            //================================================
            float length =
                sqrtf(
                    screenAxis.x * screenAxis.x +
                    screenAxis.y * screenAxis.y
                );

            if (length > 0.0001f)
            {
                //============================================
                // 正規化
                //============================================
                screenAxis.x /= length;
                screenAxis.y /= length;

                //============================================
                // マウス移動量を画面上の軸方向へ投影
                //
                // 軸に沿ってどれだけドラッグしたかを求める
                //============================================
                float projected =
                    mouseDelta.x * screenAxis.x +
                    mouseDelta.y * screenAxis.y;

                //============================================
                // Scale速度
                //============================================
                float scaleSpeed =
                    0.01f;

                //============================================
                // Scale変化量
                //============================================
                float scaleAmount =
                    projected * scaleSpeed;

                //============================================
                // 毎フレーム開始時Scaleに戻す
                //============================================
                obj.transform.scale =
                    m_scaleDragStartScale;

                //============================================
                // 操作中の軸だけScale変更
                //============================================
                switch (m_activeAxis)
                {
                case GizmoAxis::X:
                    obj.transform.scale.x =
                        m_scaleDragStartScale.x +
                        scaleAmount;
                    break;

                case GizmoAxis::Y:
                    obj.transform.scale.y =
                        m_scaleDragStartScale.y +
                        scaleAmount;
                    break;

                case GizmoAxis::Z:
                    obj.transform.scale.z =
                        m_scaleDragStartScale.z +
                        scaleAmount;
                    break;

                default:
                    break;
                }

                //============================================
                // Scale下限
                //
                // 0になると行列が潰れる
                // マイナスになると反転する
                //============================================
                obj.transform.scale.x =
                    (std::max)(0.01f, obj.transform.scale.x);

                obj.transform.scale.y =
                    (std::max)(0.01f, obj.transform.scale.y);

                obj.transform.scale.z =
                    (std::max)(0.01f, obj.transform.scale.z);
            }
        }

        return;
    }
}
void DebugEditor::UpdateRotateGizmoDrag()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->input ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    // =========================
    // RotateGizmo中心
    // =========================
    XMFLOAT3 origin =
        obj.transform.position;

  

    // =========================
    // ドラッグ開始
    // =========================
    if (!m_isDraggingGizmo &&
        m_hoveredAxis != GizmoAxis::None &&
        m_context->input->IsActionPressed(InputAction::Decide))
    {
        //====================================================
        // Gizmoドラッグ状態へ移行
        //====================================================
        m_dragStartTransform =obj.transform;
        m_isDraggingGizmo = true;
        m_activeAxis = m_hoveredAxis;
        m_dragObjectIndex = m_selectedObjectIndex;

        //====================================================
        // ドラッグ開始時のQuaternion姿勢を保存
        //
        // ドラッグ中はこの姿勢を基準にして、
        // 回転差分Quaternionを合成する
        //====================================================
        m_rotateDragStartQuat =
            obj.transform.rotationQuat;

        //====================================================
        // ドラッグ開始時の回転軸を保存
        //
        // ドラッグ中にObjectが回転しても、
        // 操作用リング平面がブレないようにする
        //====================================================
        m_rotateDragAxis =
            GetAxisDirection(
                m_activeAxis,
                obj
            );

        // マウスRay作成
        Ray ray =
            CreateMouseRay();

        XMFLOAT3 hitPoint;

        // マウスRayと回転リング平面の交点を取得
        if (!IntersectRayPlane(
            ray,
            origin,
            m_rotateDragAxis,
            hitPoint))
        {
            m_isDraggingGizmo = false;
            m_activeAxis = GizmoAxis::None;
            m_dragObjectIndex = -1;
            return;
        }

        // 中心 → 開始Hit位置 のベクトル
        XMVECTOR startVec =
            XMLoadFloat3(&hitPoint) -
            XMLoadFloat3(&origin);

        startVec =
            XMVector3Normalize(startVec);

        XMStoreFloat3(
            &m_rotateDragStartVector,
            startVec
        );

        return;
    }

    // =========================
    // ドラッグ中
    // =========================
    if (m_isDraggingGizmo &&
        m_context->input->IsActionDown(InputAction::Decide))
    {
        Ray ray =
            CreateMouseRay();

        XMFLOAT3 hitPoint;

        // 現在のマウスRayと
        // ドラッグ開始時のリング平面の交点
        if (!IntersectRayPlane(
            ray,
            origin,
            m_rotateDragAxis,
            hitPoint))
        {
            return;
        }

        // 開始ベクトル
        XMVECTOR startVec =
            XMLoadFloat3(&m_rotateDragStartVector);

        startVec =
            XMVector3Normalize(startVec);

        // 現在ベクトル
        XMVECTOR currentVec =
            XMLoadFloat3(&hitPoint) -
            XMLoadFloat3(&origin);

        currentVec =
            XMVector3Normalize(currentVec);

        // 回転軸
        XMVECTOR axisVec =
            XMLoadFloat3(&m_rotateDragAxis);

        axisVec =
            XMVector3Normalize(axisVec);

        // =========================
        // startVec から currentVec への角度差
        // =========================

        // cos成分
        float dot;

        XMStoreFloat(
            &dot,
            XMVector3Dot(startVec, currentVec)
        );

        dot =
            (std::max)(
                -1.0f,
                (std::min)(1.0f, dot)
                );

        // sin成分
        XMVECTOR cross =
            XMVector3Cross(startVec, currentVec);

        float sinValue;

        XMStoreFloat(
            &sinValue,
            XMVector3Dot(axisVec, cross)
        );

        // 符号付き角度
        float angle =
            atan2f(
                sinValue,
                dot
            );
        //====================================================
        // ドラッグ開始時のQuaternion姿勢
        //====================================================
        XMVECTOR startQuat =
            XMLoadFloat4(
                &m_rotateDragStartQuat
            );

        startQuat =
            XMQuaternionNormalize(startQuat);

        //====================================================
        // ドラッグ中の回転差分Quaternionを作成
        //
        // axisVec : ドラッグ開始時に保存した回転軸
        // angle   : startVec → currentVec の符号付き角度
        //====================================================
        XMVECTOR rotateAxis;

        if (m_gizmoSpace == GizmoSpace::World)
        {
            rotateAxis = axisVec;
        }
        else
        {
            switch (m_activeAxis)
            {
            case GizmoAxis::X:
                rotateAxis = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
                break;

            case GizmoAxis::Y:
                rotateAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                break;

            case GizmoAxis::Z:
                rotateAxis = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                break;

            default:
                return;
            }
        }

        rotateAxis =
            XMVector3Normalize(rotateAxis);

        XMVECTOR deltaQuat =
            XMQuaternionRotationAxis(
                rotateAxis,
                angle
            );

        deltaQuat =
            XMQuaternionNormalize(deltaQuat);
        //====================================================
        // Quaternion合成
        //
        // World軸回転:
        //   result = deltaQuat * startQuat
        //
        // m_rotateDragAxis はGetAxisDirection()から取得した
        // World空間上の軸なので、左から差分回転を掛ける
        //====================================================
        XMVECTOR resultQuat;

        if (m_gizmoSpace == GizmoSpace::World)
        {
            // World軸回転
            resultQuat =
                XMQuaternionMultiply(
                    startQuat,
                    deltaQuat
                );
        }
        else
        {
            // Local軸回転
            resultQuat =
                XMQuaternionMultiply(
                    deltaQuat,
                    startQuat
                );
        }
        resultQuat =
            XMQuaternionNormalize(resultQuat);

        XMStoreFloat4(
            &obj.transform.rotationQuat,
            resultQuat
        );
        //====================================================
        // TransformへQuaternion姿勢を保存
        //====================================================
        XMStoreFloat4(
            &obj.transform.rotationQuat,
            resultQuat
        );

        //====================================================
        // 念のため正規化
        //
        // 回転操作を繰り返したときの誤差蓄積を防ぐ
        //====================================================
        obj.transform.NormalizeRotation();

        //========================================================
        // GizmoでQuaternionを変更したので、
        // Inspector表示用Euler角も同期する
        //========================================================
        obj.transform.SyncEulerFromQuaternion();

        return;
    }

   
}
void DebugEditor::UpdateGizmoHover()
{
    switch (m_gizmoMode)
    {
    case GizmoMode::Move:

        UpdateMoveGizmoHover();
        break;
    case GizmoMode::Scale:
        UpdateScaleGizmoHover();
        break;
    case GizmoMode::Rotate:
        UpdateRotateGizmoHover();
        break;
    }
}
void DebugEditor::UpdateMoveGizmoHover()
{
    //選択されていた場合ここで止める
    if (m_isDraggingGizmo)
    {
        m_hoveredAxis = m_activeAxis;
        return;
    }
    // 初期化
    m_hoveredAxis = GizmoAxis::None;

    // =========================
    // Contextチェック
    // =========================
    if (!m_context ||
        !m_context->objects ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    // 選択中Object取得
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    // Model未設定
    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    // =========================
    // Bounds取得
    // =========================
    const XMFLOAT3& min =
        obj.model->GetBoundsMin();

    const XMFLOAT3& max =
        obj.model->GetBoundsMax();

    // Boundsサイズ
    XMFLOAT3 size =
    {
        max.x - min.x,
        max.y - min.y,
        max.z - min.z
    };

    // 最大サイズ
    float maxExtent =
        (std::max)(
            size.x,
            (std::max)(size.y, size.z)
            );

    

    // =========================
    // Gizmo中心
    // =========================
    XMFLOAT3 localGizmoOrigin =
    {
        (min.x + max.x) * 0.5f,
        max.y + maxExtent * 0.1f,
        (min.z + max.z) * 0.5f
    };

    // =========================
    // World変換
    // =========================
    XMMATRIX world =
        obj.transform.GetWorldMatrix();

    XMVECTOR originVec =
        XMLoadFloat3(&localGizmoOrigin);

    originVec =
        XMVector3TransformCoord(
            originVec,
            world
        );

    XMFLOAT3 origin;

    XMStoreFloat3(
        &origin,
        originVec
    );

    // =========================
// 画面サイズ一定Gizmo
// =========================
    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx =
        origin.x - cameraPos.x;

    float dy =
        origin.y - cameraPos.y;

    float dz =
        origin.z - cameraPos.z;

    float distance =
        sqrtf(
            dx * dx +
            dy * dy +
            dz * dz
        );

    // 画面上でのGizmoの長さ(px)
    float targetPixelLength =
        120.0f;

    // 画面高さ
    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    // カメラの縦FOV
    float fovY =
        m_context->camera->GetFovY();

    // distance地点での画面縦方向ワールド高さ
    float viewHeightAtDistance =
        2.0f *
        distance *
        tanf(fovY * 0.5f);

    // 1pxあたりのワールドサイズ
    float worldPerPixel =
        viewHeightAtDistance / screenHeight;

    // Gizmoのワールド長さ
    float gizmoLength =
        targetPixelLength * worldPerPixel;

    // 原点から少し離す
    float startOffset =
        gizmoLength * 0.1f;

    // =========================
// Gizmo軸方向
// =========================
    XMFLOAT3 xDir =
        GetAxisDirection(GizmoAxis::X, obj);

    XMFLOAT3 yDir =
        GetAxisDirection(GizmoAxis::Y, obj);

    XMFLOAT3 zDir =
        GetAxisDirection(GizmoAxis::Z, obj);

    // =========================
    // X軸
    // =========================
    XMFLOAT3 xStart =
    {
        origin.x + xDir.x * startOffset,
        origin.y + xDir.y * startOffset,
        origin.z + xDir.z * startOffset
    };

    XMFLOAT3 xEnd =
    {
        origin.x + xDir.x * gizmoLength,
        origin.y + xDir.y * gizmoLength,
        origin.z + xDir.z * gizmoLength
    };

    // =========================
    // Y軸
    // =========================
    XMFLOAT3 yStart =
    {
        origin.x + yDir.x * startOffset,
        origin.y + yDir.y * startOffset,
        origin.z + yDir.z * startOffset
    };

    XMFLOAT3 yEnd =
    {
        origin.x + yDir.x * gizmoLength,
        origin.y + yDir.y * gizmoLength,
        origin.z + yDir.z * gizmoLength
    };

    // =========================
    // Z軸
    // =========================
    XMFLOAT3 zStart =
    {
        origin.x + zDir.x * startOffset,
        origin.y + zDir.y * startOffset,
        origin.z + zDir.z * startOffset
    };

    XMFLOAT3 zEnd =
    {
        origin.x + zDir.x * gizmoLength,
        origin.y + zDir.y * gizmoLength,
        origin.z + zDir.z * gizmoLength
    };

    POINT mousePos;
    GetCursorPos(&mousePos);
    ScreenToClient(GetActiveWindow(), &mousePos);

    DirectX::XMFLOAT2 mouseScreen =
    {
        static_cast<float>(mousePos.x),
        static_cast<float>(mousePos.y)
    };

    DirectX::XMFLOAT2 xStartScreen;
    DirectX::XMFLOAT2 xEndScreen;
    DirectX::XMFLOAT2 yStartScreen;
    DirectX::XMFLOAT2 yEndScreen;
    DirectX::XMFLOAT2 zStartScreen;
    DirectX::XMFLOAT2 zEndScreen;

    bool xVisible =
        WorldToScreen(xStart, xStartScreen) &&
        WorldToScreen(xEnd, xEndScreen);

    bool yVisible =
        WorldToScreen(yStart, yStartScreen) &&
        WorldToScreen(yEnd, yEndScreen);

    bool zVisible =
        WorldToScreen(zStart, zStartScreen) &&
        WorldToScreen(zEnd, zEndScreen);

    float threshold = 12.0f; // ピクセル単位

    float nearest = FLT_MAX;

    if (xVisible)
    {
        float xDistance =
            DistancePointToSegment2D(
                mouseScreen,
                xStartScreen,
                xEndScreen
            );

        if (xDistance < threshold &&
            xDistance < nearest)
        {
            nearest = xDistance;
            m_hoveredAxis = GizmoAxis::X;
        }
    }

    if (yVisible)
    {
        float yDistance =
            DistancePointToSegment2D(
                mouseScreen,
                yStartScreen,
                yEndScreen
            );

        if (yDistance < threshold &&
            yDistance < nearest)
        {
            nearest = yDistance;
            m_hoveredAxis = GizmoAxis::Y;
        }
    }

    if (zVisible)
    {
        float zDistance =
            DistancePointToSegment2D(
                mouseScreen,
                zStartScreen,
                zEndScreen
            );

        if (zDistance < threshold &&
            zDistance < nearest)
        {
            nearest = zDistance;
            m_hoveredAxis = GizmoAxis::Z;
        }
    }

    // =========================
    // 先端Box Hover判定
    // =========================

    // 先端Boxサイズ
    float tipSize =
        gizmoLength * 0.08f;

    // マウスRay
    Ray ray =
        CreateMouseRay();

    // =========================
    // X先端Box
    // =========================
    {
        DirectX::XMFLOAT3 tipMin =
        {
            xEnd.x - tipSize,
            xEnd.y - tipSize,
            xEnd.z - tipSize
        };

        DirectX::XMFLOAT3 tipMax =
        {
            xEnd.x + tipSize,
            xEnd.y + tipSize,
            xEnd.z + tipSize
        };

        float hitDistance = 0.0f;

        if (IntersectRayAABB(
            ray,
            tipMin,
            tipMax,
            hitDistance))
        {
            if (hitDistance < nearest)
            {
                nearest = hitDistance;
                m_hoveredAxis = GizmoAxis::X;
            }
        }
    }

    // =========================
    // Y先端Box
    // =========================
   
    {
        DirectX::XMFLOAT3 tipMin =
        {
            yEnd.x - tipSize,
            yEnd.y - tipSize,
            yEnd.z - tipSize
        };

        DirectX::XMFLOAT3 tipMax =
        {
            yEnd.x + tipSize,
            yEnd.y + tipSize,
            yEnd.z + tipSize
        };

        float hitDistance = 0.0f;

        if (IntersectRayAABB(
            ray,
            tipMin,
            tipMax,
            hitDistance))
        {
            if (hitDistance < nearest)
            {
                nearest = hitDistance;
                m_hoveredAxis = GizmoAxis::Y;
            }
        }
    }

    // =========================
    // Z先端Box
    // =========================
    {
        DirectX::XMFLOAT3 tipMin =
        {
            zEnd.x - tipSize,
            zEnd.y - tipSize,
            zEnd.z - tipSize
        };

        DirectX::XMFLOAT3 tipMax =
        {
            zEnd.x + tipSize,
            zEnd.y + tipSize,
            zEnd.z + tipSize
        };

        float hitDistance = 0.0f;

        if (IntersectRayAABB(
            ray,
            tipMin,
            tipMax,
            hitDistance))
        {
            if (hitDistance < nearest)
            {
                nearest = hitDistance;
                m_hoveredAxis = GizmoAxis::Z;
            }
        }
    }
}
void DebugEditor::UpdateScaleGizmoHover()
{
    //========================================================
    // Scale専用Hover
    //
    // 仕組みはMoveと同じだが、
    // Scale Gizmo専用として分離する
    //========================================================

    if (m_isDraggingGizmo)
    {
        m_hoveredAxis = m_activeAxis;
        return;
    }

    m_hoveredAxis = GizmoAxis::None;


    if (!m_context ||
        !m_context->objects ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    XMFLOAT3 origin =
        obj.transform.position;

    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx = origin.x - cameraPos.x;
    float dy = origin.y - cameraPos.y;
    float dz = origin.z - cameraPos.z;

    float distance =
        sqrtf(dx * dx + dy * dy + dz * dz);

    float targetPixelLength = 120.0f;

    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float fovY =
        m_context->camera->GetFovY();

    float viewHeightAtDistance =
        2.0f * distance * tanf(fovY * 0.5f);

    float worldPerPixel =
        viewHeightAtDistance / screenHeight;

    float gizmoLength =
        targetPixelLength * worldPerPixel;

    float startOffset =
        gizmoLength * 0.1f;

    XMFLOAT3 xDir = GetAxisDirection(GizmoAxis::X, obj);
    XMFLOAT3 yDir = GetAxisDirection(GizmoAxis::Y, obj);
    XMFLOAT3 zDir = GetAxisDirection(GizmoAxis::Z, obj);

    auto CheckAxis =
        [&](GizmoAxis axis, const XMFLOAT3& dir)
        {
            XMFLOAT3 start =
            {
                origin.x + dir.x * startOffset,
                origin.y + dir.y * startOffset,
                origin.z + dir.z * startOffset
            };

            XMFLOAT3 end =
            {
                origin.x + dir.x * gizmoLength,
                origin.y + dir.y * gizmoLength,
                origin.z + dir.z * gizmoLength
            };

            XMFLOAT2 startScreen;
            XMFLOAT2 endScreen;

            if (!WorldToScreen(start, startScreen) ||
                !WorldToScreen(end, endScreen))
            {
                return;
            }

            POINT mousePos;
            GetCursorPos(&mousePos);
            ScreenToClient(GetActiveWindow(), &mousePos);

            XMFLOAT2 mouseScreen =
            {
                static_cast<float>(mousePos.x),
                static_cast<float>(mousePos.y)
            };

            float distance =
                DistancePointToSegment2D(
                    mouseScreen,
                    startScreen,
                    endScreen
                );

            float threshold = 12.0f;

            if (distance < threshold)
            {
                m_hoveredAxis = axis;
            }
        };

    //========================================================
    // 中央Uniform ScaleハンドルHover判定
    //========================================================
    {
        XMFLOAT2 originScreen;

        if (WorldToScreen(origin, originScreen))
        {
            POINT mousePos;
            GetCursorPos(&mousePos);
            ScreenToClient(GetActiveWindow(), &mousePos);

            XMFLOAT2 mouseScreen =
            {
                static_cast<float>(mousePos.x),
                static_cast<float>(mousePos.y)
            };

            float dx =
                mouseScreen.x - originScreen.x;

            float dy =
                mouseScreen.y - originScreen.y;

            float distance =
                sqrtf(dx * dx + dy * dy);

            float centerThreshold =
                18.0f;

            if (distance < centerThreshold)
            {
                m_hoveredAxis =
                    GizmoAxis::Center;

                return;
            }
        }
    }

    CheckAxis(GizmoAxis::X, xDir);
    CheckAxis(GizmoAxis::Y, yDir);
    CheckAxis(GizmoAxis::Z, zDir);
}
void DebugEditor::UpdateRotateGizmoHover()
{
    // ========================================
    // ドラッグ中は
    // ActiveAxisを優先表示
    // ========================================
    if (m_isDraggingGizmo)
    {
        m_hoveredAxis =
            m_activeAxis;

        return;
    }

    // Hover解除
    m_hoveredAxis =
        GizmoAxis::None;

    // ========================================
    // Contextチェック
    // ========================================
    if (!m_context ||
        !m_context->objects ||
        !m_context->camera ||
        !m_context->renderer ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >=
        static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    // ========================================
    // 選択中Object
    // ========================================
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    // ========================================
    // Gizmo中心
    // ========================================
    XMFLOAT3 origin =
        obj.transform.position;

    // ========================================
    // Gizmo半径計算
    // DrawRotateGizmo() と同じ
    // ========================================
    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx =
        origin.x - cameraPos.x;

    float dy =
        origin.y - cameraPos.y;

    float dz =
        origin.z - cameraPos.z;

    float distance =
        sqrtf(
            dx * dx +
            dy * dy +
            dz * dz
        );

    float targetPixelRadius =
        80.0f;

    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float fovY =
        m_context->camera->GetFovY();

    float viewHeightAtDistance =
        2.0f *
        distance *
        tanf(fovY * 0.5f);

    float worldPerPixel =
        viewHeightAtDistance /
        screenHeight;

    float radius =
        targetPixelRadius *
        worldPerPixel;

    // ========================================
    // Hover許容幅
    //
    // リングの太さ
    // ========================================
    float ringThickness =
        worldPerPixel * 12.0f;

    // ========================================
    // MouseRay生成
    // ========================================
    Ray ray =
        CreateMouseRay();

    float nearest =
        FLT_MAX;

    // ========================================
    // リング判定ラムダ
    // ========================================
    auto CheckRingHover =
        [&](GizmoAxis gizmoAxis,
            const XMFLOAT3& axis)
        {
            XMFLOAT3 hitPoint;

            // ====================================
            // Rayとリング平面の交点取得
            // ====================================
            if (!IntersectRayPlane(
                ray,
                origin,
                axis,
                hitPoint))
            {
                return;
            }

            // ====================================
            // 中心から交点までの距離
            // ====================================
            float dx =
                hitPoint.x - origin.x;

            float dy =
                hitPoint.y - origin.y;

            float dz =
                hitPoint.z - origin.z;

            float hitRadius =
                sqrtf(
                    dx * dx +
                    dy * dy +
                    dz * dz
                );

            // ====================================
            // リング半径との差
            // ====================================
            float diff =
                fabsf(
                    hitRadius -
                    radius
                );

            // ====================================
            // リング付近ならHover
            // ====================================
            if (diff < ringThickness &&
                diff < nearest)
            {
                nearest =
                    diff;

                m_hoveredAxis =
                    gizmoAxis;
            }
        };

    // ========================================
    // 各軸方向取得
    // Local対応
    // ========================================
    XMFLOAT3 xAxis =
        GetAxisDirection(
            GizmoAxis::X,
            obj
        );

    XMFLOAT3 yAxis =
        GetAxisDirection(
            GizmoAxis::Y,
            obj
        );

    XMFLOAT3 zAxis =
        GetAxisDirection(
            GizmoAxis::Z,
            obj
        );

    // ========================================
    // Xリング
    // ========================================
    CheckRingHover(
        GizmoAxis::X,
        xAxis
    );

    // ========================================
    // Yリング
    // ========================================
    CheckRingHover(
        GizmoAxis::Y,
        yAxis
    );

    // ========================================
    // Zリング
    // ========================================
    CheckRingHover(
        GizmoAxis::Z,
        zAxis
    );
}
void DebugEditor::EndGizmoDragIfNeeded()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->input ||
        !m_isDraggingGizmo)
    {
        return;
    }

    if (m_context->input->IsActionDown(InputAction::Decide))
    {
        return;
    }

    if (m_dragObjectIndex < 0 ||
        m_dragObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        m_isDraggingGizmo = false;
        m_activeAxis = GizmoAxis::None;
        m_hoveredAxis = GizmoAxis::None;
        m_dragObjectIndex = -1;
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_dragObjectIndex];

    PushTransformCommand(
        m_dragObjectIndex,
        m_dragStartTransform,
        obj.transform
    );

    m_isDraggingGizmo = false;
    m_activeAxis = GizmoAxis::None;
    m_hoveredAxis = GizmoAxis::None;
    m_dragObjectIndex = -1;
}
void DebugEditor::UpdateFocusSelected()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->camera ||
        !m_context->input)
    {
        return;
    }

    if (!m_context->input->IsEditorActionPressed(
        EditorInputAction::FocusSelected))
    {
        return;
    }

    if (m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    float focusDistance =
        8.0f;

    m_context->camera->Focus(
        obj.transform.position,
        focusDistance
    );
}

void DebugEditor::UpdateGizmoMode()
{
    if (!m_context || !m_context->input)
    {
        return;
    }

    if (m_context->input->IsEditorActionPressed(
        EditorInputAction::GizmoMove))
    {
        m_gizmoMode = GizmoMode::Move;
    }

    if (m_context->input->IsEditorActionPressed(
        EditorInputAction::GizmoRotate))
    {
        m_gizmoMode = GizmoMode::Rotate;
    }

    if (m_context->input->IsEditorActionPressed(
        EditorInputAction::GizmoScale))
    {
        m_gizmoMode = GizmoMode::Scale;
    }
}

void DebugEditor::BeginFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void DebugEditor::Update()
{
    if (!m_context)
    {
        return;
    }
    // =========================
    // 入力 / 操作系
    // =========================
    // カメラ移動はEditing/Playing両方で使うため、モードに関係なく常時有効にする
    UpdateFreeCamera();
    UpdateFocusSelected();

    // オブジェクト選択・ギズモ操作はEditingモードの時だけ
    // (Playingモードでは同じ左クリックをBuildController/RoadSystemが使うため)
    if (m_context->mode == GameMode::Editing)
    {
        if (m_context->input->IsEditorActionComboPressed(EditorInputAction::Undo))
        {
            Undo();
        }

        if (m_context->input->IsEditorActionComboPressed(EditorInputAction::Redo))
        {
            Redo();
        }
        UpdateGizmoMode();
        UpdateGizmoHover();
        UpdatePicking();

        switch (m_gizmoMode)
        {
        case GizmoMode::Move:
            UpdateDragging();
            break;
        case GizmoMode::Scale:
            UpdateScaleGizmoDrag();
            break;
        case GizmoMode::Rotate:

            UpdateRotateGizmoDrag();
            break;


        }
        EndGizmoDragIfNeeded();

        // =========================
        // Gizmo Hover判定
        // =========================
        UpdateGizmoHover();
    }
}

void DebugEditor::Draw()
{
    if (!m_context)
    {
        return;
    }

    // BOX/ギズモの表示はEditingモードの時だけ(Playing中は建物/道路の見た目を邪魔しない)
    if (m_context->mode == GameMode::Editing)
    {
        // BOXの当たり判定の表示
        if (m_showAllBounds)
        {
            DrawAllObjectBounds();
        }
        else if (m_showSelectedBounds)
        {
            DrawSelectedObjectBounds();
        }

        // 軸表示
        switch (m_gizmoMode)
        {
        case GizmoMode::Move:
            DrawMoveGizmo();
            break;
        case GizmoMode::Scale:
            DrawScaleGizmo();
            break;
        case GizmoMode::Rotate:
            DrawRotateGizmo();
            break;


        }
    }

    DrawHierarchyView();
    DrawInspectorView();
    DrawEditorSettingsView();
    DrawDebugView();
}
// 選択中ObjectのOBBを描画する
void DebugEditor::DrawSelectedObjectBounds()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->debugRenderer ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    m_context->debugRenderer->AddOBB(
        obj.model->GetBoundsMin(),
        obj.model->GetBoundsMax(),
        obj.transform.GetWorldMatrix(),
        { 1.0f, 1.0f, 0.0f, 1.0f }
    );
}

// 全ObjectのOBBを描画する
void DebugEditor::DrawAllObjectBounds()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->debugRenderer)
    {
        return;
    }

    for (auto& obj : *m_context->objects)
    {
        if (!obj.model)
        {
            continue;
        }

        m_context->debugRenderer->AddOBB(
            obj.model->GetBoundsMin(),
            obj.model->GetBoundsMax(),
            obj.transform.GetWorldMatrix(),
            { 0.0f, 1.0f, 0.0f, 1.0f }
        );
    }
}
void DebugEditor::DrawPerformance()
{
    
        if (m_context->time)
        {
            ImGui::Text("FPS : %.1f", m_context->time->GetFPS());
            ImGui::Text("DeltaTime : %.4f", m_context->time->GetDeltaTime());
        }

        if (m_context->renderer)
        {
            ImGui::Text(
                "VSync : %s",
                m_context->renderer->IsVSyncEnabled() ? "ON" : "OFF"
            );
        }
    
}

void DebugEditor::DrawObjects()
{
    if (!m_context->objects)
    {
        return;
    }

        int index = 0;

        for (const auto& obj : *m_context->objects)
        {
            // オブジェクトの種類を文字列にする(Building/Road/Agent/Environment)
            const char* kindLabel = "Environment";
            switch (obj.kind)
            {
            case ObjectKind::Building: kindLabel = "Building"; break;
            case ObjectKind::Road:     kindLabel = "Road";     break;
            case ObjectKind::Agent:    kindLabel = "Agent";    break;
            default: break;
            }

            std::string label =
                "[" + std::string(kindLabel) + "] GameObject " + std::to_string(index) +
                " (#" + std::to_string(obj.id) + ")";

            bool selected =
                (m_selectedObjectIndex == index);

            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_selectedObjectIndex = index;
            }

            ++index;
        }
    
}

void DebugEditor::DrawInspector()
{
   
        if (!m_context->objects ||
            m_selectedObjectIndex < 0 ||
            m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
        {
            ImGui::Text("No object selected.");
            return;
        }

        GameObject& selectedObject =
            (*m_context->objects)[m_selectedObjectIndex];

        //========================================================
        // 種類ごとの詳細情報(Building/Agent)
        //========================================================
        if (selectedObject.kind == ObjectKind::Building)
        {
            ImGui::Text("Building Type: %s", GetBuildingDefinition(selectedObject.buildingType).name);
            ImGui::Separator();
        }
        else if (selectedObject.kind == ObjectKind::Agent && m_context->demandSystem)
        {
            std::string summary = m_context->demandSystem->GetAgentDebugSummary(selectedObject.id);
            if (!summary.empty())
            {
                ImGui::TextUnformatted(summary.c_str());
            }
            ImGui::Separator();
        }

        //========================================================
        // Position
        //========================================================
        ImGui::DragFloat3(
            "Position",
            &selectedObject.transform.position.x,
            0.1f
        );

        if (ImGui::IsItemActivated())
        {
            m_dragStartTransform =
                selectedObject.transform;
        }

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            PushTransformCommand(
                m_selectedObjectIndex,
                m_dragStartTransform,
                selectedObject.transform
            );
        }

        //========================================================
        // Rotation
        //========================================================
        float rotationDegrees[3] =
        {
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.x),
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.y),
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.z)
        };

        if (ImGui::DragFloat3(
            "Rotation",
            rotationDegrees,
            1.0f))
        {
            DirectX::XMFLOAT3 euler =
            {
                DirectX::XMConvertToRadians(rotationDegrees[0]),
                DirectX::XMConvertToRadians(rotationDegrees[1]),
                DirectX::XMConvertToRadians(rotationDegrees[2])
            };

            selectedObject.transform.SetRotationEuler(euler);
        }

        if (ImGui::IsItemActivated())
        {
            m_dragStartTransform =
                selectedObject.transform;
        }

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            PushTransformCommand(
                m_selectedObjectIndex,
                m_dragStartTransform,
                selectedObject.transform
            );
        }

        //========================================================
        // Scale
        //========================================================
        ImGui::DragFloat3(
            "Scale",
            &selectedObject.transform.scale.x,
            0.01f,
            0.01f,
            100.0f
        );

        if (ImGui::IsItemActivated())
        {
            m_dragStartTransform =
                selectedObject.transform;
        }

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            PushTransformCommand(
                m_selectedObjectIndex,
                m_dragStartTransform,
                selectedObject.transform
            );
        }
    
}

void DebugEditor::DrawEditorSettings()
{
    
        const char* moveModeItems[] =
        {
            "Camera Plane",
            "XZ Plane",
            "XY Plane",
            "YZ Plane"
        };

        int currentMoveMode =
            static_cast<int>(m_dragMoveMode);

        if (ImGui::Combo(
            "Move Mode",
            &currentMoveMode,
            moveModeItems,
            IM_ARRAYSIZE(moveModeItems)))
        {
            m_dragMoveMode =
                static_cast<DragMoveMode>(currentMoveMode);
        }

        int currentGizmoSpace =
            static_cast<int>(m_gizmoSpace);

        const char* gizmoSpaceItems[] =
        {
            "World",
            "Local"
        };

        if (ImGui::Combo(
            "Gizmo Space",
            &currentGizmoSpace,
            gizmoSpaceItems,
            IM_ARRAYSIZE(gizmoSpaceItems)))
        {
            m_gizmoSpace =
                static_cast<GizmoSpace>(currentGizmoSpace);
        }
    
}

void DebugEditor::DrawHierarchyView()
{
    float windowHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float margin =
        8.0f;

    float leftWidth =
        280.0f;

    float debugHeight =
        280.0f;

    float bottomHeight =
        220.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            margin,
            margin + debugHeight
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            leftWidth,
            windowHeight - debugHeight - bottomHeight - margin * 3.0f
        ),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "Hierarchy",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize
    );

    DrawObjects();

    ImGui::Separator();

    ImGui::Text(
        "Selected Index : %d",
        m_selectedObjectIndex
    );

    ImGui::End();
}
void DebugEditor::DrawInspectorView()
{
    float windowWidth =
        static_cast<float>(
            m_context->renderer->GetWindowWidth()
            );

    float windowHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float margin =
        8.0f;

    float rightWidth =
        360.0f;

    float bottomHeight =
        220.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            windowWidth - rightWidth - margin,
            margin
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            rightWidth,
            windowHeight - bottomHeight - margin * 2.0f
        ),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "Inspector",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize
    );

    DrawInspector();

    ImGui::End();
}

void DebugEditor::DrawEditorSettingsView()
{
    float windowWidth =
        static_cast<float>(
            m_context->renderer->GetWindowWidth()
            );

    float windowHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float margin =
        8.0f;

    float leftWidth =
        280.0f;

    float rightWidth =
        360.0f;

    float bottomHeight =
        220.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            leftWidth + margin * 2.0f,
            windowHeight - bottomHeight - margin
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            windowWidth - leftWidth - rightWidth - margin * 4.0f,
            bottomHeight
        ),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "Editor Settings",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize
    );

    DrawEditorSettings();

    ImGui::End();
}

void DebugEditor::DrawDebugView()
{
    float margin =
        8.0f;

    float leftWidth =
        280.0f;

    float debugHeight =
        280.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            margin,
            margin
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            leftWidth,
            debugHeight
        ),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "Debug",
        nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize
    );

    DrawPerformance();

    // =========================
    // Play / Stop
    // =========================
    // Playing: BuildController/RoadSystemの入力が有効(オブジェクト選択・ギズモは無効)
    // Editing: オブジェクト選択・ギズモが有効(建物/道路配置は無効)
    if (m_context->mode == GameMode::Playing)
    {
        if (ImGui::Button("Stop", ImVec2(80, 0)))
        {
            m_context->mode = GameMode::Editing;
        }
        ImGui::SameLine();
        ImGui::Text("Playing");
    }
    else
    {
        if (ImGui::Button("Play", ImVec2(80, 0)))
        {
            m_context->mode = GameMode::Playing;
        }
        ImGui::SameLine();
        ImGui::Text("Editing");
    }

    if (ImGui::CollapsingHeader("Debug Draw"))
    {
        ImGui::Checkbox(
            "Show Selected Bounds",
            &m_showSelectedBounds
        );

        ImGui::Checkbox(
            "Show All Bounds",
            &m_showAllBounds
        );
    }

    ImGui::End();
}

void DebugEditor::DrawMoveGizmo()
{
    // =========================
    // Contextチェック
    // =========================
    if (!m_context ||
        !m_context->objects ||
        !m_context->debugRenderer ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    // 選択中Object取得
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    // Model未設定なら終了
    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    // =========================
    // Model Bounds取得
    // =========================
    const XMFLOAT3& min =
        obj.model->GetBoundsMin();

    const XMFLOAT3& max =
        obj.model->GetBoundsMax();

    // =========================
    // Boundsサイズ
    // =========================
    XMFLOAT3 size =
    {
        max.x - min.x,
        max.y - min.y,
        max.z - min.z
    };

    // 最も大きいサイズ
    float maxExtent =
        (std::max)(
            size.x,
            (std::max)(size.y, size.z)
            );

   

    // =========================
    // ローカル空間で
    // Bounds上面中央を作る
    // =========================
    XMFLOAT3 localGizmoOrigin =
    {
        (min.x + max.x) * 0.5f,
        max.y + maxExtent * 0.1f,
        (min.z + max.z) * 0.5f
    };

    // =========================
    // World変換
    // =========================
    XMMATRIX world =
        obj.transform.GetWorldMatrix();

    XMVECTOR originVec =
        XMLoadFloat3(&localGizmoOrigin);

    originVec =
        XMVector3TransformCoord(
            originVec,
            world
        );

    XMFLOAT3 origin;

    XMStoreFloat3(
        &origin,
        originVec
    );

    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx =
        origin.x - cameraPos.x;

    float dy =
        origin.y - cameraPos.y;

    float dz =
        origin.z - cameraPos.z;

    float distance =
        sqrtf(
            dx * dx +
            dy * dy +
            dz * dz
        );

    // =========================
    // 画面サイズ一定Gizmo
    // =========================

    // 画面上でのGizmoの長さ(px)
    float targetPixelLength =
        120.0f;

    // 画面高さ
    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    // カメラの縦FOV
    float fovY =
        m_context->camera->GetFovY();

    // カメラ距離distanceにおける
    // 画面縦方向のワールド高さ
    float viewHeightAtDistance =
        2.0f *
        distance *
        tanf(fovY * 0.5f);

    // 1pxあたりのワールドサイズ
    float worldPerPixel =
        viewHeightAtDistance / screenHeight;

    // Gizmoのワールド長さ
    float gizmoLength =
        targetPixelLength * worldPerPixel;

    // 原点から少し離す
    float startOffset =
        gizmoLength * 0.1f;

    // =========================
    // Gizmo軸方向
    // =========================
    XMFLOAT3 xDir =
        GetAxisDirection(GizmoAxis::X, obj);

    XMFLOAT3 yDir =
        GetAxisDirection(GizmoAxis::Y, obj);

    XMFLOAT3 zDir =
        GetAxisDirection(GizmoAxis::Z, obj);

    // =========================
    // X軸
    // =========================
    XMFLOAT3 xStart =
    {
        origin.x + xDir.x * startOffset,
        origin.y + xDir.y * startOffset,
        origin.z + xDir.z * startOffset
    };

    XMFLOAT3 xEnd =
    {
        origin.x + xDir.x * gizmoLength,
        origin.y + xDir.y * gizmoLength,
        origin.z + xDir.z * gizmoLength
    };

    // =========================
    // Y軸
    // =========================
    XMFLOAT3 yStart =
    {
        origin.x + yDir.x * startOffset,
        origin.y + yDir.y * startOffset,
        origin.z + yDir.z * startOffset
    };

    XMFLOAT3 yEnd =
    {
        origin.x + yDir.x * gizmoLength,
        origin.y + yDir.y * gizmoLength,
        origin.z + yDir.z * gizmoLength
    };

    // =========================
    // Z軸
    // =========================
    XMFLOAT3 zStart =
    {
        origin.x + zDir.x * startOffset,
        origin.y + zDir.y * startOffset,
        origin.z + zDir.z * startOffset
    };

    XMFLOAT3 zEnd =
    {
        origin.x + zDir.x * gizmoLength,
        origin.y + zDir.y * gizmoLength,
        origin.z + zDir.z * gizmoLength
    };
    // =========================
    // 表示用軸
    // =========================
    // ドラッグ中は activeAxis を優先
    GizmoAxis displayAxis =
        m_isDraggingGizmo ?
        m_activeAxis :
        m_hoveredAxis;

    // =========================
    // X軸描画 赤
    // =========================
    DirectX::XMFLOAT4 xColor =
    {
        0.7f,
        0.0f,
        0.0f,
        1.0f
    };

    if (displayAxis == GizmoAxis::X)
    {
        xColor =
        {
            1.0f,
            1.0f,
            0.0f,
            1.0f
        };
    }

    m_context->debugRenderer->AddLine(
        xStart,
        xEnd,
        xColor
    );

    {
        DirectX::XMFLOAT3 offset =
        {
            0.0f,
            0.02f,
            0.0f
        };

        DirectX::XMFLOAT3 xStart2 =
        {
            xStart.x + offset.x,
            xStart.y + offset.y,
            xStart.z + offset.z
        };

        DirectX::XMFLOAT3 xEnd2 =
        {
            xEnd.x + offset.x,
            xEnd.y + offset.y,
            xEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            xStart2,
            xEnd2,
            xColor
        );
    }

    if (displayAxis == GizmoAxis::X)
    {
        DirectX::XMFLOAT3 offset =
        {
            0.0f,
            -0.02f,
            0.0f
        };

        DirectX::XMFLOAT3 xStart3 =
        {
            xStart.x + offset.x,
            xStart.y + offset.y,
            xStart.z + offset.z
        };

        DirectX::XMFLOAT3 xEnd3 =
        {
            xEnd.x + offset.x,
            xEnd.y + offset.y,
            xEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            xStart3,
            xEnd3,
            xColor
        );
    }
    DrawMoveAxisArrow(
        xEnd,
        xDir,
        xColor,
        gizmoLength
    );

    // =========================
    // X文字表示
    // =========================
    DirectX::XMFLOAT2 xScreenPos;

    if (WorldToScreen(xEnd, xScreenPos))
    {
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(xScreenPos.x, xScreenPos.y),
            IM_COL32(255, 0, 0, 255),
            "X"
        );
    }

    // =========================
    // Y軸描画 緑
    // =========================
    DirectX::XMFLOAT4 yColor =
    {
        0.0f,
        0.7f,
        0.0f,
        1.0f
    };

    if (displayAxis == GizmoAxis::Y)
    {
        yColor =
        {
            1.0f,
            1.0f,
            0.0f,
            1.0f
        };
    }

    m_context->debugRenderer->AddLine(
        yStart,
        yEnd,
        yColor
    );

    {
        DirectX::XMFLOAT3 offset =
        {
            0.02f,
            0.0f,
            0.0f
        };

        DirectX::XMFLOAT3 yStart2 =
        {
            yStart.x + offset.x,
            yStart.y + offset.y,
            yStart.z + offset.z
        };

        DirectX::XMFLOAT3 yEnd2 =
        {
            yEnd.x + offset.x,
            yEnd.y + offset.y,
            yEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            yStart2,
            yEnd2,
            yColor
        );
    }

    if (displayAxis == GizmoAxis::Y)
    {
        DirectX::XMFLOAT3 offset =
        {
            -0.02f,
            0.0f,
            0.0f
        };

        DirectX::XMFLOAT3 yStart3 =
        {
            yStart.x + offset.x,
            yStart.y + offset.y,
            yStart.z + offset.z
        };

        DirectX::XMFLOAT3 yEnd3 =
        {
            yEnd.x + offset.x,
            yEnd.y + offset.y,
            yEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            yStart3,
            yEnd3,
            yColor
        );
    }

    DrawMoveAxisArrow(
        yEnd,
        yDir,
        yColor,
        gizmoLength
    );

    // =========================
    // Y文字表示
    // =========================
    DirectX::XMFLOAT2 yScreenPos;

    if (WorldToScreen(yEnd, yScreenPos))
    {
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(yScreenPos.x, yScreenPos.y),
            IM_COL32(0, 255, 0, 255),
            "Y"
        );
    }

    // =========================
    // Z軸描画 青
    // =========================
    DirectX::XMFLOAT4 zColor =
    {
        0.0f,
        0.0f,
        0.7f,
        1.0f
    };

    if (displayAxis == GizmoAxis::Z)
    {
        zColor =
        {
            1.0f,
            1.0f,
            0.0f,
            1.0f
        };
    }

    m_context->debugRenderer->AddLine(
        zStart,
        zEnd,
        zColor
    );

    {
        DirectX::XMFLOAT3 offset =
        {
            0.02f,
            0.0f,
            0.0f
        };

        DirectX::XMFLOAT3 zStart2 =
        {
            zStart.x + offset.x,
            zStart.y + offset.y,
            zStart.z + offset.z
        };

        DirectX::XMFLOAT3 zEnd2 =
        {
            zEnd.x + offset.x,
            zEnd.y + offset.y,
            zEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            zStart2,
            zEnd2,
            zColor
        );
    }

    if (displayAxis == GizmoAxis::Z)
    {
        DirectX::XMFLOAT3 offset =
        {
            -0.02f,
            0.0f,
            0.0f
        };

        DirectX::XMFLOAT3 zStart3 =
        {
            zStart.x + offset.x,
            zStart.y + offset.y,
            zStart.z + offset.z
        };

        DirectX::XMFLOAT3 zEnd3 =
        {
            zEnd.x + offset.x,
            zEnd.y + offset.y,
            zEnd.z + offset.z
        };

        m_context->debugRenderer->AddLine(
            zStart3,
            zEnd3,
            zColor
        );
    }

    DrawMoveAxisArrow(
        zEnd,
        zDir,
        zColor,
        gizmoLength
    );
    // =========================
    // Z文字表示
    // =========================
    DirectX::XMFLOAT2 zScreenPos;

    if (WorldToScreen(zEnd, zScreenPos))
    {
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(zScreenPos.x, zScreenPos.y),
            IM_COL32(0, 120, 255, 255),
            "Z"
        );
    }
}
void DebugEditor::DrawScaleGizmo()
{
    //========================================================
    // Contextチェック
    //========================================================
    if (!m_context ||
        !m_context->objects ||
        !m_context->debugRenderer ||
        !m_context->camera ||
        !m_context->renderer ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    //========================================================
    // 選択中Object取得
    //========================================================
    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    //========================================================
    // Scale Gizmo中心
    //========================================================
    XMFLOAT3 origin =
        obj.transform.position;

    //========================================================
    // カメラとの距離を求める
    //========================================================
    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx = origin.x - cameraPos.x;
    float dy = origin.y - cameraPos.y;
    float dz = origin.z - cameraPos.z;

    float distance =
        sqrtf(dx * dx + dy * dy + dz * dz);

    //========================================================
    // 画面上でのGizmo長さ
    //========================================================
    float targetPixelLength =
        110.0f;

    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float fovY =
        m_context->camera->GetFovY();

    float viewHeightAtDistance =
        2.0f * distance * tanf(fovY * 0.5f);

    float worldPerPixel =
        viewHeightAtDistance / screenHeight;

    float gizmoLength =
        targetPixelLength * worldPerPixel;

    //========================================================
    // 中心から少し離して描画開始
    //========================================================
    float startOffset =
        gizmoLength * 0.15f;

    //========================================================
    // Scale用Boxサイズ
    //========================================================
    float boxSize =
        gizmoLength * 0.14f;

    //========================================================
    // 各軸方向取得
    //========================================================
    XMFLOAT3 xDir =
        GetAxisDirection(GizmoAxis::X, obj);

    XMFLOAT3 yDir =
        GetAxisDirection(GizmoAxis::Y, obj);

    XMFLOAT3 zDir =
        GetAxisDirection(GizmoAxis::Z, obj);

    //========================================================
    // 表示用Axis
    //========================================================
    GizmoAxis displayAxis =
        m_isDraggingGizmo ?
        m_activeAxis :
        m_hoveredAxis;

    //========================================================
    // Scale軸描画関数
    //========================================================
    auto DrawScaleAxis =
        [&](GizmoAxis axis,
            const XMFLOAT3& dir,
            const XMFLOAT4& baseColor)
        {
            XMFLOAT4 color =
                baseColor;

            //============================================
            // Hover中 / Drag中の軸は黄色
            //============================================
            if (displayAxis == axis)
            {
                color =
                {
                    1.0f,
                    1.0f,
                    0.0f,
                    1.0f
                };
            }

            //============================================
            // 軸の開始位置
            //============================================
            XMFLOAT3 start =
            {
                origin.x + dir.x * startOffset,
                origin.y + dir.y * startOffset,
                origin.z + dir.z * startOffset
            };

            //============================================
            // 軸の終了位置
            //============================================
            XMFLOAT3 end =
            {
                origin.x + dir.x * gizmoLength,
                origin.y + dir.y * gizmoLength,
                origin.z + dir.z * gizmoLength
            };

            //============================================
            // 軸ライン描画
            //============================================
            m_context->debugRenderer->AddLine(
                start,
                end,
                color
            );

            //============================================
            // Scale用先端Box描画
            //============================================
            DrawScaleAxisBox(
                end,
                color,
                boxSize
            );
        };

    //========================================================
    // X/Y/Z軸描画
    //========================================================
    DrawScaleAxis(
        GizmoAxis::X,
        xDir,
        { 0.8f, 0.0f, 0.0f, 1.0f }
    );

    DrawScaleAxis(
        GizmoAxis::Y,
        yDir,
        { 0.0f, 0.8f, 0.0f, 1.0f }
    );

    DrawScaleAxis(
        GizmoAxis::Z,
        zDir,
        { 0.0f, 0.2f, 1.0f, 1.0f }
    );

    //========================================================
    // 中央Uniform Scaleハンドル
    //========================================================
    XMFLOAT4 centerColor =
    {
        1.0f,
        0.0f,
        1.0f,
        1.0f
    };

    if (displayAxis == GizmoAxis::Center)
    {
        centerColor =
        {
            1.0f,
            1.0f,
            0.0f,
            1.0f
        };
    }

    DrawScaleAxisBox(
        origin,
        centerColor,
        boxSize * 1.2f
    );
}

void DebugEditor::DrawRotateGizmo()
{
    if (!m_context ||
        !m_context->objects ||
        !m_context->debugRenderer ||
        m_selectedObjectIndex < 0 ||
        m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[m_selectedObjectIndex];

    if (!obj.model)
    {
        return;
    }

    using namespace DirectX;

    XMFLOAT3 origin =
        obj.transform.position;

    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    float dx =
        origin.x - cameraPos.x;

    float dy =
        origin.y - cameraPos.y;

    float dz =
        origin.z - cameraPos.z;

    float distance =
        sqrtf(dx * dx + dy * dy + dz * dz);

    float targetPixelRadius =
        80.0f;

    float screenHeight =
        static_cast<float>(
            m_context->renderer->GetWindowHeight()
            );

    float fovY =
        m_context->camera->GetFovY();

    float viewHeightAtDistance =
        2.0f * distance * tanf(fovY * 0.5f);

    float worldPerPixel =
        viewHeightAtDistance / screenHeight;

    float radius =
        targetPixelRadius * worldPerPixel;

    const int segmentCount = 64;

   

    XMFLOAT3 xAxis =
        GetAxisDirection(GizmoAxis::X, obj);

    XMFLOAT3 yAxis =
        GetAxisDirection(GizmoAxis::Y, obj);

    XMFLOAT3 zAxis =
        GetAxisDirection(GizmoAxis::Z, obj);

    GizmoAxis displayAxis =
        m_isDraggingGizmo ?
        m_activeAxis :
        m_hoveredAxis;

    XMFLOAT4 xColor =
        displayAxis == GizmoAxis::X ?
        XMFLOAT4{ 1.0f, 1.0f, 0.0f, 1.0f } :
        XMFLOAT4{ 0.8f, 0.0f, 0.0f, 1.0f };

    XMFLOAT4 yColor =
        displayAxis == GizmoAxis::Y ?
        XMFLOAT4{ 1.0f, 1.0f, 0.0f, 1.0f } :
        XMFLOAT4{ 0.0f, 0.8f, 0.0f, 1.0f };

    XMFLOAT4 zColor =
        displayAxis == GizmoAxis::Z ?
        XMFLOAT4{ 1.0f, 1.0f, 0.0f, 1.0f } :
        XMFLOAT4{ 0.0f, 0.2f, 1.0f, 1.0f };
   
    // ========================================
    // リングの見た目の太さ
    // ========================================
    // worldPerPixel を使うことで、
    // カメラ距離が変わっても画面上の太さが安定する
    // ========================================
    float ringWidth =
        worldPerPixel * 4.0f;

    // ========================================
    // Xリング描画
    // ========================================
    DrawRotateRing(origin, xAxis, radius - ringWidth, xColor);
    DrawRotateRing(origin, xAxis, radius, xColor);
    DrawRotateRing(origin, xAxis, radius + ringWidth, xColor);

    // ========================================
    // Yリング描画
    // ========================================
    DrawRotateRing(origin, yAxis, radius - ringWidth, yColor);
    DrawRotateRing(origin, yAxis, radius, yColor);
    DrawRotateRing(origin, yAxis, radius + ringWidth, yColor);

    // ========================================
    // Zリング描画
    // ========================================
    DrawRotateRing(origin, zAxis, radius - ringWidth, zColor);
    DrawRotateRing(origin, zAxis, radius, zColor);
    DrawRotateRing(origin, zAxis, radius + ringWidth, zColor);
}

void DebugEditor::DrawMoveAxisArrow(const DirectX::XMFLOAT3& end,const DirectX::XMFLOAT3& dir,const DirectX::XMFLOAT4& color,float gizmoLength)
{
    using namespace DirectX;

    float arrowSize =
        gizmoLength * 0.12f;

    XMFLOAT3 cameraPos =
        m_context->camera->GetPosition();

    XMVECTOR centerVec =
        XMLoadFloat3(&end);

    XMVECTOR cameraVec =
        XMLoadFloat3(&cameraPos);

    XMVECTOR viewDir =
        XMVector3Normalize(
            cameraVec - centerVec
        );

    XMVECTOR axisVec =
        XMVector3Normalize(
            XMLoadFloat3(&dir)
        );

    XMVECTOR sideVec =
        XMVector3Cross(
            viewDir,
            axisVec
        );

    sideVec =
        XMVector3Normalize(sideVec);

    XMVECTOR tip =
        centerVec +
        axisVec * arrowSize;

    XMVECTOR left =
        centerVec -
        axisVec * arrowSize * 0.6f +
        sideVec * arrowSize;

    XMVECTOR right =
        centerVec -
        axisVec * arrowSize * 0.6f -
        sideVec * arrowSize;

    XMFLOAT3 v0;
    XMFLOAT3 v1;
    XMFLOAT3 v2;

    XMStoreFloat3(&v0, tip);
    XMStoreFloat3(&v1, left);
    XMStoreFloat3(&v2, right);

    m_context->debugRenderer->AddLine(v0, v1, color);
    m_context->debugRenderer->AddLine(v1, v2, color);
    m_context->debugRenderer->AddLine(v2, v0, color);
}

void DebugEditor::DrawScaleAxisBox( const DirectX::XMFLOAT3& end,const DirectX::XMFLOAT4& color,float boxSize)
{
    DirectX::XMFLOAT3 boxMin =
    {
        end.x - boxSize,
        end.y - boxSize,
        end.z - boxSize
    };

    DirectX::XMFLOAT3 boxMax =
    {
        end.x + boxSize,
        end.y + boxSize,
        end.z + boxSize
    };

    m_context->debugRenderer->AddOBB(
        boxMin,
        boxMax,
        DirectX::XMMatrixIdentity(),
        color
    );
}

void DebugEditor::DrawRotateRing(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& axis,float radius,const DirectX::XMFLOAT4& color)
{
    using namespace DirectX;

    const int segmentCount = 64;

    XMVECTOR axisVec =
        XMVector3Normalize(
            XMLoadFloat3(&axis)
        );

    XMVECTOR up =
        XMVectorSet(
            0.0f,
            1.0f,
            0.0f,
            0.0f
        );

    if (fabsf(
        XMVectorGetX(
            XMVector3Dot(axisVec, up)
        )) > 0.99f)
    {
        up =
            XMVectorSet(
                1.0f,
                0.0f,
                0.0f,
                0.0f
            );
    }

    XMVECTOR right =
        XMVector3Normalize(
            XMVector3Cross(up, axisVec)
        );

    XMVECTOR forward =
        XMVector3Normalize(
            XMVector3Cross(axisVec, right)
        );

    XMVECTOR center =
        XMLoadFloat3(&origin);

    XMFLOAT3 prevPoint;

    for (int i = 0; i <= segmentCount; ++i)
    {
        float t =
            static_cast<float>(i) /
            static_cast<float>(segmentCount);

        float angle =
            t * DirectX::XM_2PI;

        XMVECTOR point =
            center +
            right * cosf(angle) * radius +
            forward * sinf(angle) * radius;

        XMFLOAT3 currentPoint;

        XMStoreFloat3(
            &currentPoint,
            point
        );

        if (i > 0)
        {
            m_context->debugRenderer->AddLine(
                prevPoint,
                currentPoint,
                color
            );
        }

        prevPoint =
            currentPoint;
    }
}

void DebugEditor::EndFrame()
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

Ray DebugEditor::CreateMouseRay()
{
    // Camera::ScreenPointToRay に委譲。
    // 以前はここで GetCursorPos/ScreenToClient を直接呼んでいたが、
    // InputManager::GetMousePosition() 経由に統一した
    // (BuildController/RoadSystemも同じ経路でマウス位置を取得する)。
    POINT mousePos = m_context->input->GetMousePosition();

    return m_context->camera->ScreenPointToRay(
        mousePos,
        m_context->renderer->GetWindowWidth(),
        m_context->renderer->GetWindowHeight());
}

bool DebugEditor::WorldToScreen(const DirectX::XMFLOAT3& worldPos,DirectX::XMFLOAT2& screenPos)
{
    if (!m_context ||
        !m_context->camera ||
        !m_context->renderer)
    {
        return false;
    }

    using namespace DirectX;

    XMMATRIX view =
        m_context->camera->GetViewMatrix();

    XMMATRIX projection =
        m_context->camera->GetProjectionMatrix();

    XMMATRIX viewProjection =
        view * projection;

    XMVECTOR pos =
        XMLoadFloat3(&worldPos);

    // World座標 → Clip空間
    XMVECTOR clipPos =
        XMVector3TransformCoord(pos, viewProjection);

    XMFLOAT3 ndc;
    XMStoreFloat3(&ndc, clipPos);

    // カメラ後方や範囲外ならfalse
    if (ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return false;
    }

    float width =
        static_cast<float>(m_context->renderer->GetWindowWidth());

    float height =
        static_cast<float>(m_context->renderer->GetWindowHeight());

    // NDC(-1〜1) → Screen座標
    screenPos.x =
        (ndc.x + 1.0f) * 0.5f * width;

    screenPos.y =
        (1.0f - ndc.y) * 0.5f * height;

    return true;
}

// Ray と平面の交点を求める
bool DebugEditor::IntersectRayPlane(const Ray& ray,const DirectX::XMFLOAT3& planePoint,const DirectX::XMFLOAT3& planeNormal,DirectX::XMFLOAT3& hitPoint)
{
    // Math/Collision.h の共有版に委譲(BuildController/RoadSystemからも同じ実装を使う)
    return ::IntersectRayPlane(ray, planePoint, planeNormal, hitPoint);
}

// Ray と 線分の最短距離を求める
float DebugEditor::DistanceRayToSegment(const Ray& ray,const DirectX::XMFLOAT3& segStart,const DirectX::XMFLOAT3& segEnd)
{
    using namespace DirectX;

    // =========================
    // Ray情報
    // =========================
    XMVECTOR rayOrigin =
        XMLoadFloat3(&ray.origin);

    // Ray方向を正規化
    XMVECTOR rayDir =
        XMVector3Normalize(
            XMLoadFloat3(&ray.direction)
        );

    // =========================
    // 線分情報
    // =========================
    XMVECTOR a =
        XMLoadFloat3(&segStart);

    XMVECTOR b =
        XMLoadFloat3(&segEnd);

    // 線分ベクトル
    XMVECTOR seg =
        b - a;

    // Ray原点 → 線分開始点
    XMVECTOR diff =
        rayOrigin - a;

    // =========================
    // 線分長さ
    // =========================
    float segLengthSq;

    XMStoreFloat(
        &segLengthSq,
        XMVector3LengthSq(seg)
    );

    // 長さ0なら無効
    if (segLengthSq < 0.0001f)
    {
        return FLT_MAX;
    }

    // =========================
    // 内積計算
    // =========================
    float rayDotSeg;
    float rayDotDiff;
    float segDotDiff;

    XMStoreFloat(
        &rayDotSeg,
        XMVector3Dot(rayDir, seg)
    );

    XMStoreFloat(
        &rayDotDiff,
        XMVector3Dot(rayDir, diff)
    );

    XMStoreFloat(
        &segDotDiff,
        XMVector3Dot(seg, diff)
    );

    // =========================
    // 係数
    // =========================
    float aCoef = 1.0f;
    float bCoef = rayDotSeg;
    float cCoef = segLengthSq;
    float dCoef = rayDotDiff;
    float eCoef = segDotDiff;

    // 分母
    float denom =
        aCoef * cCoef - bCoef * bCoef;

    // Ray上パラメータ
    float rayT = 0.0f;

    // 線分上パラメータ
    float segT = 0.0f;

    // =========================
    // 平行でない場合
    // =========================
    if (fabsf(denom) > 0.0001f)
    {
        // Ray上最近点
        rayT =
            (bCoef * eCoef - cCoef * dCoef) / denom;

        // 線分上最近点
        segT =
            (aCoef * eCoef - bCoef * dCoef) / denom;
    }

    // =========================
    // Clamp
    // =========================

    // Rayは前方向のみ
    rayT =
        (std::max)(rayT, 0.0f);

    // 線分範囲内にClamp
    segT =
        (std::max)(
            0.0f,
            (std::min)(1.0f, segT)
            );

    // =========================
    // 最近点取得
    // =========================
    XMVECTOR closestRay =
        rayOrigin + rayDir * rayT;

    XMVECTOR closestSeg =
        a + seg * segT;

    // 最近点差分
    XMVECTOR delta =
        closestRay - closestSeg;

    // =========================
    // 距離計算
    // =========================
    float distance;

    XMStoreFloat(
        &distance,
        XMVector3Length(delta)
    );

    return distance;
}

float DebugEditor::DistancePointToSegment2D(const DirectX::XMFLOAT2& point,const DirectX::XMFLOAT2& segStart,const DirectX::XMFLOAT2& segEnd)
{
    float vx = segEnd.x - segStart.x;
    float vy = segEnd.y - segStart.y;

    float wx = point.x - segStart.x;
    float wy = point.y - segStart.y;

    float lengthSq = vx * vx + vy * vy;

    if (lengthSq < 0.0001f)
    {
        float dx = point.x - segStart.x;
        float dy = point.y - segStart.y;
        return sqrtf(dx * dx + dy * dy);
    }

    float t =
        (wx * vx + wy * vy) / lengthSq;

    t =
        (std::max)(
            0.0f,
            (std::min)(1.0f, t)
            );

    float closestX =
        segStart.x + vx * t;

    float closestY =
        segStart.y + vy * t;

    float dx =
        point.x - closestX;

    float dy =
        point.y - closestY;

    return sqrtf(dx * dx + dy * dy);
}

DirectX::XMFLOAT3 DebugEditor::GetAxisDirection( GizmoAxis axis,const GameObject& obj)
{
    using namespace DirectX;

    // Worldモード
    if (m_gizmoSpace == GizmoSpace::World)
    {
        switch (axis)
        {
        case GizmoAxis::X:
            return { 1.0f, 0.0f, 0.0f };

        case GizmoAxis::Y:
            return { 0.0f, 1.0f, 0.0f };

        case GizmoAxis::Z:
            return { 0.0f, 0.0f, 1.0f };

        default:
            return { 0.0f, 0.0f, 0.0f };
        }
    }

    //========================================================
    // Localモード
    // Quaternionから回転行列作成
    //========================================================
    XMVECTOR q =
        XMLoadFloat4(
            &obj.transform.rotationQuat
        );

    q =
        XMQuaternionNormalize(q);

    XMMATRIX rotation =
        XMMatrixRotationQuaternion(q);

    XMVECTOR dir;

    switch (axis)
    {
    case GizmoAxis::X:

        dir =
            XMVector3TransformNormal(
                XMVectorSet(
                    1.0f, 0.0f, 0.0f, 0.0f),
                rotation
            );
        break;

    case GizmoAxis::Y:

        dir =
            XMVector3TransformNormal(
                XMVectorSet(
                    0.0f, 1.0f, 0.0f, 0.0f),
                rotation
            );
        break;

    case GizmoAxis::Z:

        dir =
            XMVector3TransformNormal(
                XMVectorSet(
                    0.0f, 0.0f, 1.0f, 0.0f),
                rotation
            );
        break;

    default:
        return { 0.0f, 0.0f, 0.0f };
    }

    XMFLOAT3 result;

    XMStoreFloat3(
        &result,
        XMVector3Normalize(dir)
    );

    return result;
}

void DebugEditor::UpdateFreeCamera()
{
    if (!m_context || !m_context->camera || !m_context->input)
    {
        return;
    }

    if (m_isDraggingGizmo || m_isDraggingObject)
    {
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse)
    {
        m_isFreeCameraActive = false;
        return;
    }

    bool look =
        m_context->input->IsEditorActionDown(
            EditorInputAction::FreeCameraLook
        );

    POINT currentMousePos =
        m_context->input->GetMousePosition();

    if (!look)
    {
        m_isFreeCameraActive = false;
        m_prevMousePos = currentMousePos;
        return;
    }

    if (!m_isFreeCameraActive)
    {
        m_isFreeCameraActive = true;
        m_prevMousePos = currentMousePos;
        return;
    }

    float mouseDeltaX =
        static_cast<float>(currentMousePos.x - m_prevMousePos.x);

    float mouseDeltaY =
        static_cast<float>(currentMousePos.y - m_prevMousePos.y);

    m_prevMousePos = currentMousePos;

    m_context->camera->AddYawPitch(
        mouseDeltaX * m_freeCameraRotateSpeed,
        -mouseDeltaY * m_freeCameraRotateSpeed
    );

    float moveSpeed =
        m_freeCameraMoveSpeed;

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraFast))
    {
        moveSpeed *= 3.0f;
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraForward))
    {
        m_context->camera->MoveForward(moveSpeed);
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraBackward))
    {
        m_context->camera->MoveForward(-moveSpeed);
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraRight))
    {
        m_context->camera->MoveRight(moveSpeed);
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraLeft))
    {
        m_context->camera->MoveRight(-moveSpeed);
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraUp))
    {
        m_context->camera->MoveUp(moveSpeed);
    }

    if (m_context->input->IsEditorActionDown(EditorInputAction::FreeCameraDown))
    {
        m_context->camera->MoveUp(-moveSpeed);
    }
}

bool DebugEditor::NearlyEqual(float a,float b,float epsilon)
{
    return fabsf(a - b) < epsilon;
}

bool DebugEditor::NearlyEqual(const DirectX::XMFLOAT3& a,const DirectX::XMFLOAT3& b,float epsilon)
{
    return
        NearlyEqual(a.x, b.x, epsilon) &&
        NearlyEqual(a.y, b.y, epsilon) &&
        NearlyEqual(a.z, b.z, epsilon);
}

bool DebugEditor::NearlyEqual(const DirectX::XMFLOAT4& a,const DirectX::XMFLOAT4& b,float epsilon)
{
    return
        NearlyEqual(a.x, b.x, epsilon) &&
        NearlyEqual(a.y, b.y, epsilon) &&
        NearlyEqual(a.z, b.z, epsilon) &&
        NearlyEqual(a.w, b.w, epsilon);
}

bool DebugEditor::IsSameTransform( const Transform& a,const Transform& b)
{
    return
        NearlyEqual(a.position, b.position) &&
        NearlyEqual(a.scale, b.scale) &&
        NearlyEqual(a.rotationQuat, b.rotationQuat);
}

void DebugEditor::PushTransformCommand(int objectIndex,const Transform& before,const Transform& after)
{
    //========================================================
    // before と after が同じならUndo履歴に登録しない
    //
    // クリックしただけ、数値を触ったが変化していない、
    // などの不要なUndoを防ぐ
    //========================================================
    if (IsSameTransform(before, after))
    {
        return;
    }

    TransformCommand command;
    command.objectIndex = objectIndex;
    command.before = before;
    command.after = after;

    m_undoStack.push_back(command);

    //========================================================
    // Undo履歴上限
    //
    // 古い履歴から削除して、
    // 最大 MaxUndoCount 件までにする
    //========================================================
    if (m_undoStack.size() > MaxUndoCount)
    {
        m_undoStack.erase(
            m_undoStack.begin()
        );
    }

    //========================================================
    // 新しい操作をしたらRedo履歴は消す
    //========================================================
    m_redoStack.clear();
}
void DebugEditor::Undo()
{
    if (m_undoStack.empty())
    {
        return;
    }

    TransformCommand command =
        m_undoStack.back();

    m_undoStack.pop_back();

    if (!m_context ||
        !m_context->objects ||
        command.objectIndex < 0 ||
        command.objectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[command.objectIndex];

    obj.transform =
        command.before;

    m_selectedObjectIndex =
        command.objectIndex;

    m_redoStack.push_back(command);
}

void DebugEditor::Redo()
{
    if (m_redoStack.empty())
    {
        return;
    }

    TransformCommand command =
        m_redoStack.back();

    m_redoStack.pop_back();

    if (!m_context ||
        !m_context->objects ||
        command.objectIndex < 0 ||
        command.objectIndex >= static_cast<int>(m_context->objects->size()))
    {
        return;
    }

    GameObject& obj =
        (*m_context->objects)[command.objectIndex];

    obj.transform =
        command.after;

    m_selectedObjectIndex =
        command.objectIndex;

    m_undoStack.push_back(command);
}



void DebugEditor::Finalize()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}



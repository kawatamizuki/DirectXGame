#pragma once
#include <vector>
#include <DirectXMath.h>
#include "LightingState.h"
#include "Transform.h"
#include "Camera.h"

class Renderer;
class Model;
class OffscreenTarget;

// モデルをどの方向から見るか。回転(ヨー・ピッチ)とズームだけを持つ、モデルの周りを回るカメラの指定。
struct PreviewView
{
    float yaw = 0.55f;    // 横方向の角度(ラジアン)。0=+Z側(エンジンの前方側)から見る、π=-Z側(背面)から見る
    float pitch = 0.46f;  // 上下の角度(ラジアン)。0=真横、正=上から見下ろす
    float zoom = 1.0f;    // 1が標準。大きいほど近づく
};

// モデルを、OffscreenTarget(画面とは別のテクスチャ)に、1枚の絵として描く。
// サムネイルの生成と、アセットブラウザの詳細表示で共通して使う。
// モデルは、中心を原点に、最大の辺が一定の大きさになるよう拡縮して描く(BuildControllerの自動フィットと同じ考え方)ので、
// 単位や原点の位置がバラバラな素材でも、同じ大きさ・同じ位置で見える。
class ModelPreviewRenderer
{
public:
    // rendererは呼び出し側が所有する(借りるだけ)。
    void Initialize(Renderer* renderer);

    // modelをtargetに描く。光はlightingで描き(ゲーム内の時刻とは無関係にできる)、描き終わったらRendererの光を元に戻す。
    // 描画先・ビューポートは、描画前の状態に戻る。
    void Render(const Model& model, OffscreenTarget& target, const PreviewView& view, const LightingState& lighting);

    // 複数のモデルを、1つの世界に置いて描く(ビューワー用)。modelsの各項目は、置く位置・向き・大きさが入った Transform。
    // targetに、view の軌道カメラで描く。光はlightingで描き、描き終わったらRendererの光を元に戻す。描画先・ビューポートは元に戻る。
    // brightness は背景の明るさの倍率(1=標準の薄い空色、0に近いほど暗い)。
    struct SceneModel
    {
        const Model* model = nullptr;
        Transform transform;
    };
    struct SceneView
    {
        DirectX::XMFLOAT3 target = { 0.0f, 0.0f, 0.0f }; // 見ている点
        float yaw = 0.0f;       // 横方向の角度(ラジアン。0=+Z側から見る)
        float pitch = 0.6f;     // 上下の角度(ラジアン。正=上から見下ろす)
        float distance = 10.0f; // 見ている点からカメラまでの距離
    };
    void RenderScene(const std::vector<SceneModel>& models, OffscreenTarget& target, const SceneView& view,
        const LightingState& lighting, float backgroundBrightness = 1.0f);

    // view の軌道カメラ(ImGuiのオーバーレイを3Dの位置に合わせる、WorldToScreenなどに使う)。
    static Camera MakeOrbitCamera(const SceneView& view, float aspect);

    // modelを、中心がcenterに来るよう、最大の辺が一定の大きさ(Renderと同じ)になるよう拡縮して置く Transform。
    // yawRadians は、その場でY軸まわりに回す角度(背面から見たい時はπ)。
    static Transform MakeFittedTransform(const Model& model, const DirectX::XMFLOAT3& center, float yawRadians);

    // 並べる時の1つぶんの大きさ(MakeFittedTransformの最大の辺の長さ)。
    static float GetFittedSize();

    // モデルの最大の辺が、描画の時に何倍されるか(情報表示用)。
    static float ComputeFitScale(const Model& model);

    // 背景色(薄い空色)。サムネイルと詳細表示で同じにする。
    static const float* BackgroundColor();

private:
    Renderer* m_renderer = nullptr;
};

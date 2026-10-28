#pragma once
#include <DirectXMath.h>

class Camera;

// 街づくり(Playing)のカメラの動かし方の設定。数値は、ここだけを変えれば調整できる。
struct CityCameraSettings
{
    float pitchRadians = DirectX::XMConvertToRadians(55.0f); // 見下ろす角度(0=水平、90度=真上)。固定
    float defaultDistance = 14.0f;     // 最初のズーム(注視点からカメラまでの距離)
    float minDistance = 4.0f;          // 一番寄れる距離
    float maxDistance = 40.0f;         // 一番引ける距離
    float panSpeedPerDistance = 0.9f;  // キーでの平行移動の速さ(1秒あたり。ズームの距離に掛ける。引くほど速い=画面での速さが一定)
    float fastMultiplier = 2.5f;       // Shiftを押している間の倍率
    float zoomPerNotch = 0.85f;        // ホイール1目盛りで、距離が何倍になるか(奥へ回すと近づく)
    float smoothing = 14.0f;           // 動きのなめらかさ(大きいほど、すぐ追いつく。1秒あたり)
    float boundsMargin = 2.0f;         // 注視点が、フィールドの外へ出られる幅
};

// 1フレームぶんの入力(Game が集めて渡す。この部品は、入力の仕組みやImGuiを知らない)。
struct CityCameraInput
{
    float deltaSeconds = 0.0f;    // 前のフレームからの実時間(ゲーム内時間の倍率・停止の影響を受けない)

    float panX = 0.0f;            // キーでの左右の移動(-1=左、+1=右)
    float panZ = 0.0f;            // キーでの前後の移動(-1=手前、+1=奥)
    bool fast = false;            // 速く動かす(Shift)

    float wheelNotches = 0.0f;    // ホイールの回転量(ノッチ。奥へ回すと正=ズームイン)

    bool dragging = false;        // 地面をつかんで動かしている最中か(中ボタンを押している)
    bool cursorValid = false;     // カーソルが、ゲームビューの中で、UIの上でもないか(ズームの中心・ドラッグに使える)
    float cursorX = 0.0f;         // カーソルの位置(ゲームビューの左上が原点のピクセル)
    float cursorY = 0.0f;
    float prevCursorX = 0.0f;     // 前のフレームのカーソルの位置
    float prevCursorY = 0.0f;
    float viewWidth = 1.0f;       // ゲームビューの大きさ(ピクセル)
    float viewHeight = 1.0f;

    // 追跡: trueの間、注視点を、(followX, followZ)(地面の上の点。追跡する住民の位置)へ、なめらかに追わせる。
    // 距離(ズーム)・向きは、そのまま(ホイールのズームは使える)。キーでの移動・ドラッグは、Game側が、追跡をやめる合図に使う。
    bool followActive = false;
    float followX = 0.0f;
    float followZ = 0.0f;
};

// 街づくりゲームによくある、見下ろしのカメラ。次の2つの要素で動く。
//   ・平面の移動: WASD・矢印キー(Shiftで速く)、または、中ボタンのドラッグで「地面をつかんで」動かす。
//   ・ズーム(縮尺): ホイール。カーソルの下の地面の点が、画面の同じ場所に残るように、寄る・引く。
// 向き(yaw)と見下ろす角度(pitch)は固定。注視点は地面(y=0)の上にあり、カメラはその斜め上から見る。
// 注視点はフィールドの範囲(+余白)から出られない。目標の値(m_target…)へ、なめらかに追いつく。
//
// エディタのフリーカメラ(Editingモード)とは別物。GameがモードでどちらをUpdateするかを決める。
// 画面・入力・ImGuiには依存せず、Cameraだけに書き込むので、単体テストできる。
class CityCameraController
{
public:
    void SetSettings(const CityCameraSettings& settings) { m_settings = settings; }
    const CityCameraSettings& GetSettings() const { return m_settings; }

    // 注視点を動かせる範囲(フィールドの幅・奥行きの半分。原点が中心)。boundsMargin が足される。
    void SetBounds(float halfWidth, float halfDepth);

    // 最初の見え方に戻す(フィールドの中心を、既定のズームで、奥向きに見る)。camera に書き込む。
    void Reset(Camera& camera);

    // 今の camera(フリーカメラで動かした視点など)を引き継ぐ。視線が地面に当たる点を注視点にして、距離はそこまでの距離。
    // 向き(yaw)は今のカメラの向きを使い、見下ろす角度は設定の値にそろえる。camera にも書き込む。
    void AdoptFrom(Camera& camera);

    // 入力を反映して、camera の位置と注視点を更新する。
    void Update(const CityCameraInput& input, Camera& camera);

    // ---- 状態(確認・テスト用) ----
    const DirectX::XMFLOAT3& GetFocus() const { return m_focus; }
    float GetDistance() const { return m_distance; }
    float GetYaw() const { return m_yaw; }
    const DirectX::XMFLOAT3& GetTargetFocus() const { return m_targetFocus; }
    float GetTargetDistance() const { return m_targetDistance; }

    // ---- 計算(単体テスト用に公開) ----

    // 注視点・向き・見下ろす角度・距離から、カメラの位置を求める。
    static DirectX::XMFLOAT3 ComputeCameraPosition(const DirectX::XMFLOAT3& focus, float yaw, float pitch, float distance);

    // 画面の点(ゲームビューの左上が原点のピクセル)の下の、地面(y=0)の点。地面に当たらなければfalse。
    static bool RaycastGround(const Camera& camera, float cursorX, float cursorY, float viewWidth, float viewHeight, DirectX::XMFLOAT3& outPoint);

private:
    // 注視点 focus・距離 distance で決まる位置のカメラ(camera の投影などを引き継ぐ)。ズームやドラッグでは、画面に見えている「今の」カメラを使う。
    Camera MakeCameraAt(const Camera& camera, const DirectX::XMFLOAT3& focus, float distance) const;

    void ClampTargetFocus();
    void ApplyToCamera(Camera& camera) const;

    CityCameraSettings m_settings;

    float m_boundsHalfWidth = 10.0f;
    float m_boundsHalfDepth = 10.0f;

    float m_yaw = 0.0f;                                  // 向き(ラジアン)。0=+Z方向(奥)を見る
    DirectX::XMFLOAT3 m_focus = { 0.0f, 0.0f, 0.0f };    // 今の注視点(なめらかに動く)
    float m_distance = 14.0f;                            // 今の距離
    DirectX::XMFLOAT3 m_targetFocus = { 0.0f, 0.0f, 0.0f };  // 目標の注視点(入力が直接変える)
    float m_targetDistance = 14.0f;                          // 目標の距離
};

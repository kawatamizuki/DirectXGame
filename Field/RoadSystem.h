#pragma once
#include <vector>
#include <optional>
#include "RoadSegment.h"
#include "RoadNode.h"
#include "RoadNodeIndex.h"
#include "GridCoord.h"
#include "Model.h"
#include "RoadDrawMode.h"

struct GameContext;
class Field;
class IOccupancyGrid;
class IRoadPlacementStrategy;
class IRoadMeshGenerator;
class GridOrientationRegistry;

// フィールド上への道路配置システム(Playingモード時のみ動作)。
// 道路はグリッドに縛られず、任意の2点を直線で結べる
// (占有判定のみグリッドセル単位で行う)。
class RoadSystem
{
public:
    void Initialize(
        GameContext* context,
        Field* field,
        IOccupancyGrid* occupancy,
        IRoadPlacementStrategy* straightStrategy,
        IRoadPlacementStrategy* curveStrategy,
        IRoadMeshGenerator* meshGenerator,
        GridOrientationRegistry* orientationRegistry);

    void Update();
    void Draw();

    // 配置済みの道路区間一覧を返す(DemandSystemが道路網の読み取り・混雑度の書き込みに使う)。
    std::vector<RoadSegment>& GetSegments() { return m_segments; }
    // constからの読み取り専用アクセス(Profilerのメモリ内訳表示など、書き込み不要な用途向け)。
    const std::vector<RoadSegment>& GetSegments() const { return m_segments; }

    // 交差点・端点の一覧を返す(将来の建物回転・パラメータ補正機能から使う想定)。
    const std::vector<RoadNode>& GetNodes() const { return m_nodes; }

    // 道路を全て消して、道路が1本も無い状態に戻す(区間・ノード・置きかけのフリーハンド操作・メッシュ)。
    // 占有(IOccupancyGrid)と青マス(GridOrientationRegistry)は呼び出し側が別に消す。
    void Clear();

    // 保存した区間の一覧をそのまま設定する(現在の道路は消える)。操作の再生ではなく**状態のコピー**なので、
    // 配置ルールの検証は行わない(保存した街は、後でルールが変わってもそのまま読める)。
    // 決まった手順で作り直せる派生データを作る: 道路ノード(区間の端点から)、占有(区間ごとの矩形とマス)、道路メッシュ(最後に1回だけ)。
    // 青マスは順序に依存するため、ここでは作らない(GridOrientationRegistry::ImportCellsで保存した一覧を戻す)。
    void RestoreSegments(const std::vector<RoadSegment>& segments);

    // 道路メッシュの頂点数(Profilerのメモリ内訳表示に使う)。
    // IRoadMeshGeneratorの完全な定義はRoadSystem.cpp側でしか見えていないため、実装は.cpp側に置く。
    size_t GetMeshVertexCount() const;

private:
    // マウスのワールド着地点の吸着結果。
    struct SnapResult
    {
        DirectX::XMFLOAT3 point;      // 吸着後の座標(吸着しなければ入力そのまま)
        bool snapped = false;         // 既存の端点/区間の途中のどちらかに吸着したか
        bool requiresSplit = false;   // 確定時にSplitSegmentAtが必要か(既存区間の途中への吸着の場合true)
        size_t splitSegmentIndex = 0; // requiresSplit==trueの時、分割対象のm_segments内での添字
    };

    // マウスのワールド着地点が、既存セグメントの端点または区間の途中に近ければそこへ吸着させる。
    // 優先順位: ①既存の端点(そのまま繋がる) → ②既存区間の途中(確定時に区間の分割が必要)。
    // モードによって、①②どちらにも当てはまらなかった時のフォールバックが変わる:
    //   - Curve: 生の点のまま(自由描画を保つため何もしない)。道幅考慮(widthAware)もここだけ有効。
    //   - Straight かつ pendingStart==nullptr(1点目/起点を探している): 最も近いセルの中心にスナップ
    //     (建物もセルの中心に配置されるため、道路と建物の基準点を合わせている)。
    //   - Straight かつ pendingStart!=nullptr(2点目/終点を、*pendingStartから見て探している):
    //     近くに建物があればその向きに角度を合わせ、無ければ15度刻みに丸める。
    //     距離はどちらの場合もセル単位(cellSizeの整数倍)に丸める。
    SnapResult TryFindSnapPoint(
        const DirectX::XMFLOAT3& point,
        RoadDrawMode mode,
        const DirectX::XMFLOAT3* pendingStart = nullptr) const;

    // segmentIndexの区間を、start→splitPointとsplitPoint→endの2つに置き換える(タイプ・幅は元のまま引き継ぐ)。
    void SplitSegmentAt(size_t segmentIndex, const DirectX::XMFLOAT3& splitPoint);

    // 今から配置しようとしているモード(Straight/Curve)に対応するストラテジーを返す。
    IRoadPlacementStrategy* GetActiveStrategy() const;

    // 直線モード(2クリック確定)の入力処理。
    void UpdateStraightMode(const DirectX::XMFLOAT3& hitPoint, const SnapResult& snap);
    // 曲線モード(ドラッグでフリーハンド)の入力処理。
    void UpdateCurveMode(const DirectX::XMFLOAT3& hitPoint, const SnapResult& snap);

    // segmentを今置けるかを返す(無ければfalse)。判定は実際の形(矩形)どうしの厳密な重なりで行う:
    //   ①建物には重ならない(道路面を、建物の形と比較。ぴったり隣接はOK)。
    //   ②他の道路は横切らない(中心線が既存区間の路面と重なったら拒否)。ただし
    //     connectionPointsのどれかを路面上に含む既存区間は「繋がる相手」なので対象外。
    // 以前はワールドのマス単位の占有と照合していたため、斜め道路の脇では接続点のすぐ先の
    // マスが「埋まり」扱いになり、道路しか無い場所にも繋げられなかった。
    // CommitSegmentの実際の判定と、カーソルのホバー表示(緑/赤マーカー)の両方から
    // 同じ関数を使うことで、表示と実際の判定がズレないようにする。
    bool IsSegmentPlaceable(const RoadSegment& segment, const std::vector<DirectX::XMFLOAT3>& connectionPoints) const;

    // IsSegmentPlaceableに渡す接続点を集める。始点は、既存区間の途中への吸着(startRequiresSplit)か、
    // 既存の端点ノードの上にある時だけ含める。終点はendConnects(既存の道路に吸着した場合)
    // の時だけ含める(何もない場所へ誤って重ねた場合はそのまま拒否したい)。
    std::vector<DirectX::XMFLOAT3> CollectConnectionPoints(
        const DirectX::XMFLOAT3& start, bool startRequiresSplit,
        const DirectX::XMFLOAT3& end, bool endConnects) const;

    // 確定した区間をm_segmentsに追加し、占有判定・ノード情報の更新・必要な分割まで行う。
    // 通過できなければfalseを返す(その場合m_segmentsには追加されない)。
    // chainConnectionPoints: 自由描画のように複数の区間をまとめて確定する時の、連鎖全体の
    // 接続点(既定は空)。ループ状に戻ってくる軌跡でも、出発した道路のそばを通れるように
    // 全区間の判定に共通で使う。
    bool CommitSegment(
        RoadSegment segment,
        bool startRequiresSplit, size_t startSplitIndex,
        bool endConnects, bool endRequiresSplit, size_t endSplitIndex,
        const std::vector<DirectX::XMFLOAT3>& chainConnectionPoints = {});

    // 既存道路の「途中」(分割が必要な接続)へ新しい区間approachSegmentを繋げる時、
    // 接続先の区間targetSegmentIndexとほぼ直角(±30度程度)で交わっているかを返す。
    // 浅い角度での分岐は、T字路の継ぎ目がミターされない(独立した縁のまま)こともあり
    // 隙間/めくれが目立つ見た目になるため、カーブモードの途中接続はこの条件を満たす時だけ許可する。
    bool IsPerpendicularEnoughForSplit(const RoadSegment& approachSegment, size_t targetSegmentIndex) const;

    // point(区間の端点)をノードとして登録し、segmentIndexを接続一覧に追加する
    // (既存ノードと一致すればそれに追加、無ければ新規ノードを作る)。
    void RegisterSegmentEndpoint(const DirectX::XMFLOAT3& point, size_t segmentIndex);

    // segmentが確定した時、その両脇(道路の縁に接する列)に、道路の向きに揃ったローカルマスを
    // 長さ方向に隙間なく登録する(GridOrientationRegistry::TryClaimCell)。
    // 既に他の道路/建物のマスがある場所と、他の道路の路面にかかる場所には登録しない
    // (先着優先)。ownSegmentIndexはm_segments内のこの区間の番号(自分自身の路面は除外するため)。
    void ClaimOrientationNearSegment(const RoadSegment& segment, size_t ownSegmentIndex) const;

    // 近くの建物(GameContext::objectsのObjectKind::Building)の向きを、
    // kOrientationAlignDistance以内で探す。見つかればそのyaw(rotation.y)を返す。
    std::optional<float> FindNearbyBuildingYaw(const DirectX::XMFLOAT3& point) const;

    // m_curvePendingSegments全体が(始点→終点を結ぶ直線から見て)ほぼ真っ直ぐな場合、
    // 手ブレによる細かいガタつきを均した1本の直線として返す。実際に曲げている場合は
    // m_curvePendingSegmentsをそのまま返す(平滑化済みの滑らかな形を保ち、カクつかせない)。
    // ゴースト表示・有効判定・確定のすべてがこの関数の結果を使うことで、ドラッグ中の
    // 見た目と実際に確定される道路が常に一致するようにする。
    std::vector<RoadSegment> GetEffectiveCurvePendingSegments() const;

    // m_curvePendingSegments全体が、今の実際の占有状況に対して置けるか(全か無かで判定)。
    // 先頭の区間だけ始点接続(m_pendingStartRequiresSplit由来)、末尾の区間だけ終点接続
    // (m_curvePendingEndConnects)を考慮する。1区間でも無効ならfalseを返す。
    bool IsPendingCurvePathValid() const;

    // m_curvePendingSegmentsからゴーストメッシュを作り直す(有効なら緑寄り、無効なら赤寄りの色)。
    void RebuildCurveGhostMesh();

    // 確認アイコンが押された時: m_curvePendingSegments全体を実際にCommitSegmentして確定する。
    void ConfirmPendingCurve();

    // キャンセル時: 何もコミットせずバッファと状態を破棄する。
    void CancelPendingCurve();

    // m_curveAwaitingConfirm中、ドラッグを離した位置の横に確認/キャンセルの小さいImGuiアイコンを出す。
    void DrawConfirmIcon();

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
    IOccupancyGrid* m_occupancy = nullptr;
    IRoadPlacementStrategy* m_straightStrategy = nullptr;
    IRoadPlacementStrategy* m_curveStrategy = nullptr;
    IRoadMeshGenerator* m_meshGenerator = nullptr;
    GridOrientationRegistry* m_orientationRegistry = nullptr;

    std::vector<RoadSegment> m_segments;

    // 交差点・端点の永続データ(区間を1本追加/分割するたびに差分更新される)。
    std::vector<RoadNode> m_nodes;
    RoadNodeIndex m_nodeIndex;

    // Update()で計算した「今フレームの着地点(スナップ済み)」をDraw()のプレビューでも使い回す
    DirectX::XMFLOAT3 m_lastHitPoint{};
    bool m_hasLastHit = false;
    bool m_lastHitIsSnapped = false;

    // 今のカーソル位置(m_lastHitPoint)に置ける/繋げられるかどうか。
    // Draw()でカーソル位置の丸マーカーを緑(置ける)/赤(置けない)に色分けするために使う。
    bool m_hoverValid = true;

    // 置きかけの区間の始点(直線モードの1クリック目、または曲線モードのドラッグ開始点)が、
    // 既存区間の途中への吸着だった場合に備え、確定するまで分割対象を覚えておく。
    bool m_pendingStartRequiresSplit = false;
    size_t m_pendingStartSplitSegmentIndex = 0;

    // 曲線モードでドラッグ中かどうか。ドラッグ開始直後に確定する最初の1区間だけ、
    // 始点の吸着・分割(m_pendingStartRequiresSplit)を適用する。
    bool m_curveDragActive = false;
    bool m_curveFirstSegmentPending = false;

    // カーブドラッグ中に逐次確定せず貯めておく、まだ未確定の小区間一覧。
    // ドラッグ中〜確認待ちの間はここに積むだけで、m_segments/m_occupancyには一切触れない。
    std::vector<RoadSegment> m_curvePendingSegments;

    // ドラッグを離した後、確認アイコンの表示中かどうか。
    bool m_curveAwaitingConfirm = false;

    // m_curvePendingSegmentsの最後の区間の終点についての吸着情報(確定時にCommitSegmentへ渡す)。
    bool m_curvePendingEndConnects = false;
    bool m_curvePendingEndRequiresSplit = false;
    size_t m_curvePendingEndSplitIndex = 0;

    // ゴーストプレビュー用の一時モデル(確定前の半透明表示。m_segmentsには含まれない)。
    Model m_curveGhostModel;
};

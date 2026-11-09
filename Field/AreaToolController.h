#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "CommercialArea.h"

struct GameContext;
class Field;
class DebugRenderer;
class CommercialAreaManager;

// 商業エリアのツール(PlacementTool::Area)の、入力・プレビュー・描画(P2-A)。
// 操作(Playingの間):
//  ・何も無い地面をクリック: そこを中心に、区画を仮置きする(無料。他の区画と重なる場所には置けない)。置いた区画が、選ばれる。
//  ・区画の上でクリック: その区画を選ぶ。押したままドラッグして、動かせる(離した位置が、他の区画と重ならない時だけ確定。緑=置ける・赤=置けない)。
//  ・選んだ区画の、周知・削除・店舗数と進捗の表示は、画面の右の「エリア」パネル(UI/AreaPanel。操作は Game が受ける)。
//  ・Esc・右クリック: 動かしている最中ならそれをやめる。選んでいるなら選びをやめる。それ以外なら、ツールをやめる(Gameが処理する)。
// 区画の枠は、ツールを使っていない間も描く(公開済み=金色・非公開=水色)。住民の行動には、何も影響しない。
// 区画の状態の変更は、全て CommercialAreaManager を通す(このクラスは、入力と見た目だけを持つ)。
class AreaToolController
{
public:
    void Initialize(GameContext* context, Field* field, CommercialAreaManager* areas);

    // 結果の知らせ(「ここには置けません」)を、画面に出すために、ゲーム側へ渡す。
    void SetOnMessage(std::function<void(const std::string&)> callback) { m_onMessage = std::move(callback); }

    // 毎フレーム。エリアのツールの間だけ、入力を処理する。ツールをやめたら、動かしかけ・選びを外す。
    void Update();

    // 全ての区画の枠・プレビュー・選んだ区画の所属施設の枠を描く(debugRendererに足す。Flushは呼び出し側)。
    void Draw(DebugRenderer& debugRenderer) const;

    // 動かしている最中か(Esc・右クリックで、ツールをやめる前に、先に、動かしかけだけをやめるかの判断に使う)。
    bool IsDragging() const { return m_dragging; }
    // 選んでいる区画のID(0=無し)。
    uint32_t SelectedId() const { return m_selectedId; }
    void ClearSelection() { m_selectedId = 0; }
    void Select(uint32_t id) { m_selectedId = id; }

    // 動かしかけをやめる(何も変えない)。
    void Cancel();

private:
    void Notify(const std::string& text) const;

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
    CommercialAreaManager* m_areas = nullptr;

    uint32_t m_selectedId = 0;
    bool m_dragging = false;
    float m_dragOffsetX = 0.0f;         // 押した位置から、区画の中心までのずれ(掴んだ所を保つ)
    float m_dragOffsetZ = 0.0f;
    AreaRect m_dragRect;                // 動かした先の矩形
    bool m_dragValid = false;           // 動かした先が、置けるか

    bool m_hasHover = false;            // カーソルの下の地面があるか
    float m_hoverX = 0.0f;
    float m_hoverZ = 0.0f;
    AreaRect m_hoverRect;               // 今クリックしたら置かれる区画の矩形(区画の上でなければ)
    bool m_hoverValid = false;
    bool m_hoverOverArea = false;       // カーソルが、既存の区画の上か(上なら、置かずに選ぶ)

    std::function<void(const std::string&)> m_onMessage;
};

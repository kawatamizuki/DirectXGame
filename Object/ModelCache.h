#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "Model.h"

class Renderer;

// 実行中に必要になったモデル(ファイルのパスで指定)を、1回だけ読んで持っておく置き場。
// Assetsパネルからシーンへ置いた飾り(Prop)や、セーブデータから復元する飾りが使う。
//
// ・Get(path) は、同じパスなら、いつも同じModelの住所を返す(Modelはunique_ptrで持つので、後から別のモデルが増えても
//   住所が変わらない)。Propが Model* を持ち続けても安全。
// ・読み込みは Model::LoadFromFile(モデルキャッシュ BakedModelCache が効くので、2回目以降の起動では軽い)。
// ・読めなかったパスは覚えておき、毎フレーム読み直そうとしない(ドラッグ中のゴースト表示などで、毎フレーム呼ばれるため)。
// ・解放はしない(Propが指している間は、モデルが必要なため。数が増えすぎた時の整理は将来の課題)。
class ModelCache
{
public:
    // rendererは呼び出し側が所有する(借りるだけ。デバイスの取得に使う)。
    void Initialize(Renderer* renderer) { m_renderer = renderer; }

    // pathのモデルを返す。読み込めなければnullptr。
    Model* Get(const std::string& path);

    // 既に読み込み済みのモデルか(読み込みは起こさない)。
    bool IsLoaded(const std::string& path) const { return m_models.find(path) != m_models.end(); }

    // 読み込みに失敗したパスか(再試行しない)。
    bool HasFailed(const std::string& path) const { return m_failed.find(path) != m_failed.end(); }

    // 失敗の記憶を消す(ファイルを足した後に、もう一度読めるようにする)。
    void ForgetFailures() { m_failed.clear(); }

    size_t GetLoadedCount() const { return m_models.size(); }

private:
    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Model>> m_models;
    std::unordered_set<std::string> m_failed;
};

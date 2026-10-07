#pragma once
#include <map>
#include <string>
#include "ISaveable.h"

// ゲームの進行を表す名前付きの値の置き場(フラグ・マイルストーン・解放状態など)。
// 名前は自由(例: "tutorial.step"、"milestone.population100"、"unlock.largeRoad")。
// 値は数値で、真偽はfalse=0/true=1として使う。ISaveableなので、セーブデータの"flags"区画に
// そのまま保存・復元される(新しいフラグを足しても、保存の仕組みには触れなくてよい)。
// 複雑な進行(達成日時・段階の履歴など)が必要になったら、専用のISaveableに切り出すこと。
class GameFlags : public ISaveable
{
public:
    // nameの値を設定する。
    void Set(const std::string& name, double value);

    // nameの値を返す。無ければdefaultValue。
    double Get(const std::string& name, double defaultValue = 0.0) const;

    // nameが設定されているか。
    bool Has(const std::string& name) const;

    // 真偽として扱う便利関数(0以外ならtrue)。
    bool IsSet(const std::string& name) const { return Get(name, 0.0) != 0.0; }

    // 全て消す。
    void Clear() { m_values.clear(); }

    // ---- セーブ/ロード(ISaveable) ----
    const char* SaveKey() const override { return "flags"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override { Clear(); }

private:
    // 名前順に並ぶmapにしておくと、保存するファイルの項目の並びがいつも同じになる
    // (保存→読み込み→再保存でファイルが完全に一致する確認ができ、差分も見やすい)。
    std::map<std::string, double> m_values;
};

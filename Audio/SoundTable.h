#pragma once
#include <string>
#include <vector>
#include "AudioTypes.h"

class Json;

// 1つの音(効果音・曲)の設定。「音の表」の1行。
// 音を増やしたり、音量や距離を調整したりしても、コードは触らず、この表(Sounds/sounds.json)だけを変える。
struct SoundDefinition
{
    std::string id;                         // 音の名前(ゲームのコードから、この名前で鳴らす。例: "ui_click")
    std::string file;                       // 音のファイル(wav / mp3)。作業フォルダからの相対パス
    SoundCategory category = SoundCategory::Se;
    std::string tag;                        // まとめてミュートしたい時のグループ名(例: "ui")。空なら、タグ無し

    float volume = 1.0f;                    // この音だけの音量(0〜2)。素材ごとの、元の大きさのばらつきをそろえるために使う
    bool loop = false;                      // 繰り返すか(曲・環境音)
    bool stream = false;                    // 長い音を、少しずつ読みながら鳴らすか(曲向き。メモリを節約できる)

    // 距離による減衰。spatial==falseの音(ボタンの音・曲)は、距離に関係なく、同じ大きさで鳴る。
    bool spatial = false;                   // 位置を持つ音か(trueで、聞く位置からの距離で、小さくなる)
    float minDistance = 15.0f;              // この距離までは、減衰しない(ワールドの長さの単位)
    float maxDistance = 60.0f;              // この距離で、聞こえなくなる(0になる)
    float rolloff = 1.0f;                   // 減衰の強さ。1=距離に比例して直線的に小さくなる。大きいほど、近くで急に小さくなる

    int maxVoices = 4;                      // 同時に鳴らせる数。超えたら、一番古い音を止める(同じ音が重なりすぎないように)
};

// 音の表。ファイル(JSON)から読む。
class SoundTable
{
public:
    // 表のJSONから読む。壊れた行(idが無い・種類が不明など)は、飛ばして、警告を返す(全体は壊れない)。
    // 距離の範囲がおかしい行(最大が最小以下)は、直して続ける。戻り値は、読めた音の数。
    int LoadFromJson(const Json& root, std::vector<std::string>& warnings);

    // ファイルから読む。開けない・JSONとして壊れている時は、何も変えずにfalseを返し、messageに理由を入れる。
    bool LoadFromFile(const std::string& path, std::vector<std::string>& warnings, std::string& message);

    // 表をJSONにする / ファイルへ書く(エディタで調整した音量などを、音の表へ戻すため)。
    // 既定値と同じ項目は書かない(読みやすさのため。読み込む側は、無い項目を既定値で補う)。
    // 書くのは、一時ファイルに書き切ってから置き換える方法(途中で落ちても、元の表が壊れない)。
    Json ToJson() const;
    bool SaveToFile(const std::string& path, std::string& message) const;

    // idの音を返す(無ければnullptr)。
    const SoundDefinition* Find(const std::string& id) const;
    // 設定を書き換えたい時用(エディタ)。無ければnullptr。
    SoundDefinition* FindMutable(const std::string& id);
    int IndexOf(const std::string& id) const;   // 無ければ-1

    const std::vector<SoundDefinition>& GetAll() const { return m_sounds; }
    size_t Size() const { return m_sounds.size(); }

    // テスト・プログラムから、直接足す(idが重複していたら、足さずにfalse)。
    bool Add(const SoundDefinition& definition);

private:
    std::vector<SoundDefinition> m_sounds;
};

#pragma once
#include <cstddef>
#include <string>
#include <vector>

// メモリの使用量の内訳(1回ぶん)。Game が、各機能から集めて作り、MemoryPanel が表示する(Editorのビルドだけ)。
//
//  ・ここの数字は、「要素の数 × 1件の大きさ」で出した**概算**(OSが実際に確保したバイト数の正確な計測ではない)。
//  ・「ゲーム本体」と「デバッグ・検証の機能」を分けて出す(この2つだけが対象。エンジン・ランタイム・GPUドライバなどは、
//    プロセス全体との差(把握できていない分)にまとまる)。
//  ・上限(limitBytes)がある項目は、使用率と、どこで調整するか(limitNote)を出す。規模が大きくなった時に、制限・改善が要る所に気づくための物。
enum class MemoryGroup : unsigned char
{
    Game,       // ゲーム本体(建物・道路・住民・モデルなど)
    Debug       // デバッグ・検証の機能(記録・統計・タイムライン・耐久試験・素材一覧など)
};

struct MemoryItem
{
    MemoryGroup group = MemoryGroup::Game;
    std::string name;           // 項目の名前
    size_t bytes = 0;           // 使用量の概算
    std::string detail;         // 内訳の説明(件数など)
    size_t limitBytes = 0;      // この項目の上限(0=上限なし)。上限は、満杯になった時の使用量の目安
    std::string limitNote;      // 上限・調整の説明(どこで変えるか)
    bool gpu = false;           // GPU側のメモリの目安か(プロセスのメモリには、含まれないことが多い)
};

struct MemoryReport
{
    std::vector<MemoryItem> items;
    size_t gameBytes = 0;       // ゲーム本体の合計(CPU側のみ)
    size_t debugBytes = 0;      // デバッグ・検証の合計(CPU側のみ)
    size_t gpuBytes = 0;        // GPU側の目安の合計(参考。プロセスの合計には、入れない)

    void Add(MemoryItem item)
    {
        if (item.gpu)
        {
            gpuBytes += item.bytes;
        }
        else if (item.group == MemoryGroup::Game)
        {
            gameBytes += item.bytes;
        }
        else
        {
            debugBytes += item.bytes;
        }
        items.push_back(std::move(item));
    }
};

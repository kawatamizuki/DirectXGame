#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "Json.h"

struct GameContext;

// 保存時に、各システムへ渡す情報。
struct SaveContext
{
    const GameContext* game = nullptr;

    // 実行時の建物のGameObject::id → 保存データ内の建物のuid(建物の配列の添字)。
    std::unordered_map<uint32_t, int> buildingUidByObjectId;

    // 建物のobjectIdに対応するuidを返す。保存対象の建物でなければ-1。
    int UidForBuildingObject(uint32_t objectId) const
    {
        auto it = buildingUidByObjectId.find(objectId);
        return it == buildingUidByObjectId.end() ? -1 : it->second;
    }
};

// 読み込み時に、各システムへ渡す情報。
struct LoadContext
{
    GameContext* game = nullptr;

    // 保存データ内の建物のuid(添字) → 読み込みで新しく作った建物のGameObject::id。
    // GameObject::idは読み込みのたびに変わるので、他の保存データが建物を指す時は、
    // 保存時にuidで書き、読み込み時にこの表で新しいidに結び直す。
    std::vector<uint32_t> buildingObjectIds;

    // uidに対応する建物のobjectIdを返す。範囲外なら0(未割当)。
    uint32_t ObjectIdForBuildingUid(int uid) const
    {
        if (uid < 0 || static_cast<size_t>(uid) >= buildingObjectIds.size())
        {
            return 0;
        }
        return buildingObjectIds[static_cast<size_t>(uid)];
    }
};

// セーブ/ロードの対象になる状態を持つシステムが実装するインターフェース。
// ScenarioManagerに登録するだけで、そのシステムの状態が"state"の区画としてセーブデータに入る。
// 新しい要素(お金・建物レベル・チュートリアルの進行など)を足す時は、保存の中核には触らず、
// その要素がこのインターフェースを実装して登録する。
// 保存の形は「名前付きの項目」にして、読み込みでは足りない項目は既定値・知らない項目は無視すること
// (項目を足しても古いセーブが読める)。
class ISaveable
{
public:
    virtual ~ISaveable() = default;

    // "state"の中でのこのシステムの区画の名前(例: "clock")。変えると古いセーブが読めなくなる。
    virtual const char* SaveKey() const = 0;

    // この区画の形式の版。項目の意味を変えた時に上げ、Loadで古い版を読み替える。
    virtual int SaveVersion() const { return 1; }

    // 今の状態をsectionに書き込む(sectionは空のオブジェクト。"version"は呼び出し側が入れる)。
    virtual void Save(Json& section, const SaveContext& context) const = 0;

    // sectionから状態を復元する。versionは保存時の版。呼び出しの前に建物は既に復元されている。
    virtual void Load(const Json& section, int version, const LoadContext& context) = 0;

    // 保存データにこの区画が無かった時(古いセーブ)や、全消去の時に、初期状態へ戻す。
    virtual void ResetToDefault() = 0;
};

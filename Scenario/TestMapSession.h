#pragma once
#include <functional>
#include <string>
#include "TestMapStore.h"

// テストマップを使っている間の、街の入れ替えの管理(画面に依存しない。単体テストできる)。
//  ・テストマップを読み込む前の街を、メモリに控える(最初の読み込みの時だけ。テストマップから別のテストマップへ移っても、元の街は、最初の物のまま)。
//  ・「元の街に戻す」で、控えた街へ戻す。
//  ・「今の街の配置が、読み込んだテストマップと違うか」(建築・編集で変えたか)を調べる。読み込みの前の確認に使う。
//  ・ファイルは、一切、書かない(読み込みは、TestMapStore::Load = 既存のセーブと同じ検証・読み込み。控えは、メモリだけ)。
//    元のテストマップのファイルは、この操作では、変わらない。
struct TestMapSessionIO
{
    std::function<bool(std::string& bytes, std::string& message)> capture;                                          // 今の街を、セーブデータの文字列にする
    std::function<bool(const std::string& bytes, const std::string& label, std::string& message)> loadBytes;        // セーブデータの文字列から、街を置き換える
};

class TestMapSession
{
public:
    explicit TestMapSession(TestMapStore& store) : m_store(store) {}

    void SetIO(TestMapSessionIO io) { m_io = std::move(io); }

    // テストマップを読み込む。最初の読み込みなら、今の街を控えてから読み込む(読み込みに失敗したら、控えは作らない。街も壊れない)。
    bool Load(const std::string& id, std::string& message);

    // テストマップ使用中の、クイックセーブ(F5/F9・メニューのセーブ/ロード)の保存先。普段のクイックセーブ(Saves/quicksave.json)とは、別のファイル。
    // テストマップで遊んだ街を、誤って、普段の街のクイックセーブに上書きしないため。
    static constexpr const char* kQuickSavePath = "Saves/quicksave_testmap.json";
    // クイックセーブの保存先: テストマップ使用中は、上の別のファイル。そうでなければ、normalPath(普段のクイックセーブ)。
    std::string QuickSavePath(const std::string& normalPath) const { return Active().valid ? std::string(kQuickSavePath) : normalPath; }

    // path のセーブを読み込むと、テストマップの使用が終わる(控えが捨てられる)か。使用中で、テストマップのファイルでも、テスト用のクイックセーブでもなければ true。
    // 読み込みの前の確認(ScenarioManager のパネル)と、読み込んだ後の使用の終了(Game)で、同じ判定を使う。
    bool LoadWouldEndSession(const std::string& path) const;

    // テストマップの使用を、終える(街は、そのまま。使用中の表示と、読み込む前の街の控えを、捨てる)。別のセーブを読み込んだ時・「使用をやめる」で呼ぶ。
    void EndSession() { m_store.ClearActive(); ForgetOriginal(); }

    // 控えた街が、あるか(「元の街に戻す」を出せるか)。
    bool HasOriginal() const { return m_hasOriginal; }
    // 読み込む前の街へ戻す。戻したら、使用中の情報と控えを消す。控えが無ければ false。
    bool RestoreOriginal(std::string& message);
    // 控えを捨てる(戻さない)。
    void ForgetOriginal() { m_original.clear(); m_hasOriginal = false; }

    // 使用中のテストマップ(TestMapStore の Active)。
    const TestMapStore::ActiveMap& Active() const { return m_store.Active(); }

    // 今の街の配置が、使用中のテストマップを読み込んだ時と、違うか(違えば modified = true)。調べられなければ false を返す。
    bool IsLayoutModified(bool& modified) const;

private:
    TestMapStore& m_store;
    TestMapSessionIO m_io;
    std::string m_original;         // テストマップを読み込む前の街(セーブデータの文字列)
    bool m_hasOriginal = false;
};

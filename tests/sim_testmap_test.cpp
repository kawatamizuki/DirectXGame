// テストマップの保存・保護(段階1)の単体テスト。
// 受け入れ条件:
//   1. 手作業で作った街を、名前を付けて、テストマップとして保存できる(名前・説明・検証目的・注目する指標・作成日時を記録)。
//   2. 専用フォルダのファイルとして管理される(正式採用 = TestMaps/、作成途中 = TestMaps/Drafts/)。一覧で、メタ情報と、初期状態の指紋が分かる。
//   3. 既存の名前を上書きしない(大文字小文字の違い・別のフォルダでも)。名前(ファイル名)は、半角の英数字・「_」「-」だけ(パスの細工を許さない)。
//   4. 通常のセーブ処理(ScenarioManager::Save)が、テストマップの置き場へ書けないための判定(PathGuard)は、パスの書き方を変えても、すり抜けられない。
//   5. メタ情報を直しても、街の部分(初期状態の指紋)は変わらない。名前変更・移動・削除は、そのファイルだけに効く。
//   6. 壊れたファイル・関係ないファイルがあっても、一覧・操作が、落ちない。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "AtomicFile.h"
#include "Json.h"
#include "PathGuard.h"
#include "TestMapSession.h"
#include "TestMapStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;
static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static std::string Slurp(const fs::path& path)
{
    std::string bytes, error;
    ReadFileBytes(path.string(), bytes, error);
    return bytes;
}

static void Spit(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

// 街のセーブデータの見本(ScenarioManager が書く形式の、小さな物)。
static std::string WorldBytes(int marker)
{
    Json root = Json::MakeObject();
    root.Set("version", 1);
    root.Set("cellSize", 0.5);
    root.Set("lengthUnit", 1);
    Json roads = Json::MakeArray();
    Json road = Json::MakeObject();
    road.Set("width", 1);
    road.Set("marker", marker);
    roads.Push(std::move(road));
    root.Set("roads", std::move(roads));
    root.Set("buildings", Json::MakeArray());
    return root.Dump(true);
}

static void TestIds()
{
    std::string why;
    CHECK(TestMapStore::IsValidId("area_basic-01", &why));
    CHECK(TestMapStore::IsValidId("A", &why));
    CHECK(!TestMapStore::IsValidId("", &why));
    CHECK(!TestMapStore::IsValidId("../escape", &why));
    CHECK(!TestMapStore::IsValidId("a/b", &why));
    CHECK(!TestMapStore::IsValidId("a\\b", &why));
    CHECK(!TestMapStore::IsValidId("a.json", &why));
    CHECK(!TestMapStore::IsValidId("a b", &why));
    CHECK(!TestMapStore::IsValidId("テスト", &why));
    CHECK(!TestMapStore::IsValidId("-leading", &why));
    CHECK(!TestMapStore::IsValidId("_leading", &why));
    CHECK(!TestMapStore::IsValidId("CON", &why) && !TestMapStore::IsValidId("nul", &why) && !TestMapStore::IsValidId("Com1", &why) && !TestMapStore::IsValidId("lpt9", &why));
    CHECK(!TestMapStore::IsValidId("Drafts", &why) && !TestMapStore::IsValidId("drafts", &why));
    CHECK(!TestMapStore::IsValidId(std::string(49, 'a'), &why));
    CHECK(TestMapStore::IsValidId(std::string(48, 'a'), &why));
    CHECK(!TestMapStore::IsValidId("a:stream", &why));
}

static void TestPathGuard(const fs::path& sandbox)
{
    const fs::path root = sandbox / "TestMapsProtected";
    fs::create_directories(root / "Drafts");
    const std::string dir = root.string();
    auto inside = [&](const std::string& path) { return PathGuard::IsInsideDirectory(path, dir); };

    // 中(どの書き方でも、保護される)。
    CHECK(inside((root / "a.json").string()));
    CHECK(inside((root / "Drafts" / "a.json").string()));
    CHECK(inside(dir));
    std::string slashed = (root / "a.json").string();
    for (char& c : slashed) { if (c == '\\') { c = '/'; } }
    CHECK(inside(slashed));
    std::string upper = (root / "A.JSON").string();
    for (char& c : upper) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    CHECK(inside(upper));
    CHECK(inside((root / ".." / "TestMapsProtected" / "a.json").string()));
    CHECK(inside((root / "Drafts" / ".." / "a.json").string()));
    CHECK(inside((root / "." / "a.json").string()));
    CHECK(inside((root / "not_yet" / "deep" / "a.json").string()));            // まだ無いフォルダの下
    CHECK(inside(root.string() + ".\\a.json"));                                // 末尾のドット(Windowsは、同じフォルダとして扱う)
    CHECK(inside(root.string() + " \\a.json"));                                // 末尾の空白

    // 相対パス(現在のフォルダを、sandbox にして)。
    const fs::path previous = fs::current_path();
    fs::current_path(sandbox);
    CHECK(PathGuard::IsInsideDirectory("TestMapsProtected/a.json", "TestMapsProtected"));
    CHECK(PathGuard::IsInsideDirectory("./testmapsprotected\\Drafts\\a.json", "TestMapsProtected"));
    CHECK(PathGuard::IsInsideDirectory("Other/../TestMapsProtected/a.json", "TestMapsProtected"));
    CHECK(PathGuard::IsInsideDirectory("TestMapsProtected", "./TestMapsProtected/"));
    // 8.3形式の短い名前(使える環境だけ)。
    {
        wchar_t shortPath[MAX_PATH] = {};
        const DWORD n = GetShortPathNameW(root.wstring().c_str(), shortPath, MAX_PATH);
        if (n > 0 && n < MAX_PATH && fs::path(shortPath).filename() != root.filename())
        {
            CHECK(inside((fs::path(shortPath) / "a.json").string()));
        }
    }
    // 保護するフォルダが、まだ存在しない時も、書き方を変えて、すり抜けられない(Windowsは、末尾の「.」と空白を、無視して、同じフォルダを作る)。
    CHECK(PathGuard::IsInsideDirectory("NoSuchRoot/a.json", "NoSuchRoot"));
    CHECK(PathGuard::IsInsideDirectory("NoSuchRoot /a.json", "NoSuchRoot"));
    CHECK(PathGuard::IsInsideDirectory("NoSuchRoot./a.json", "NoSuchRoot"));
    CHECK(PathGuard::IsInsideDirectory("NoSuchRoot.. /Drafts/a.json", "NoSuchRoot"));
    CHECK(PathGuard::IsInsideDirectory("nosuchroot\\a.json", "NoSuchRoot"));
    CHECK(PathGuard::IsInsideDirectory("x/../NoSuchRoot/a.json", "NoSuchRoot"));
    CHECK(!PathGuard::IsInsideDirectory("NoSuchRoot2/a.json", "NoSuchRoot"));
    CHECK(!PathGuard::IsInsideDirectory("NoSuchRoo/a.json", "NoSuchRoot"));
    // 日本語など、変換できない文字を含むパスでも、落ちない(安全側 = 書かせない、か、普通の判定)。
    (void)PathGuard::IsInsideDirectory("C:\\Users\\mizuk\\OneDrive\\デスクトップ\\就職作品\\GAME\\GAME\\TestMaps\\x.json", "TestMaps");
    (void)PathGuard::IsInsideDirectory("テスト/x.json", "TestMaps");
    // 外(巻き込まない)。
    CHECK(!PathGuard::IsInsideDirectory("Saves/quicksave.json", "TestMapsProtected"));
    CHECK(!PathGuard::IsInsideDirectory("TestMapsProtectedOther/a.json", "TestMapsProtected"));    // 名前が前方一致するだけの、別のフォルダ
    CHECK(!PathGuard::IsInsideDirectory("TestMapsProtected_a.json", "TestMapsProtected"));
    CHECK(!PathGuard::IsInsideDirectory("a.json", "TestMapsProtected"));
    CHECK(!PathGuard::IsInsideDirectory("../TestMapsProtected2/a.json", "TestMapsProtected"));
    fs::current_path(previous);
    CHECK(!inside((sandbox / "Saves" / "quicksave.json").string()));
    CHECK(!inside(sandbox.string()));                                          // 親フォルダは、保護の外(中ではない)
    // 判定できない・怪しい書き方は、安全側(保護の中として、書かせない)。
    CHECK(inside(""));
    CHECK(inside((root / "a.json").string() + ":hidden"));
    CHECK(inside("\\\\?\\" + (root / "a.json").string()));
    CHECK(inside("\\\\.\\" + (root / "a.json").string()));
    CHECK(inside(std::string("TestMaps/a\x01.json")));
}

static void TestAtomicFile(const fs::path& sandbox)
{
    const fs::path file = sandbox / "atomic" / "deep" / "x.json";
    std::string error;
    CHECK(WriteFileAtomic(file.string(), "one", error));
    CHECK(Slurp(file) == "one");
    CHECK(WriteFileAtomic(file.string(), "two-two", error));
    CHECK(Slurp(file) == "two-two");
    CHECK(!fs::exists(file.string() + ".tmp"));
    std::string bytes;
    CHECK(!ReadFileBytes((sandbox / "missing.json").string(), bytes, error) && !error.empty());
}

static int CountJson(const fs::path& directory)
{
    int n = 0;
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) { return 0; }
    for (const auto& e : fs::directory_iterator(directory, ec)) { if (e.path().extension() == ".json") { ++n; } }
    return n;
}

static void TestStore(const fs::path& sandbox)
{
    const fs::path root = sandbox / "store" / "TestMaps";
    TestMapStore store(root.string());
    int worldMarker = 1;
    std::vector<std::string> loadedPaths;
    bool loadOk = true;
    TestMapWorldIO io;
    io.capture = [&](std::string& bytes, std::string&) { bytes = WorldBytes(worldMarker); return true; };
    io.loadFile = [&](const std::string& path, std::string& message) { loadedPaths.push_back(path); message = loadOk ? "ok" : "読めない"; return loadOk; };
    store.SetWorldIO(io);

    // 空の状態。
    CHECK(store.List().empty());

    // 保存(正式)。メタ情報・日本語。
    TestMapMeta meta;
    meta.name = "エリア基本形";
    meta.description = "コンビニ2軒と、カフェ1軒の、小さな商店街";
    meta.purpose = "評判の推移を、猶予の前後で確かめる";
    meta.focusMetrics = { "エリアの評判", "店舗別の売上" };
    std::string message;
    CHECK(store.SaveNew("area_basic", meta, TestMapFolder::Official, message));
    CHECK(fs::exists(root / "area_basic.json"));
    CHECK(!fs::exists(root / "area_basic.json.tmp"));
    TestMapEntry entry;
    CHECK(store.Find("area_basic", entry) && entry.valid);
    CHECK(entry.id == "area_basic" && entry.folder == TestMapFolder::Official);
    CHECK(entry.meta.name == "エリア基本形" && entry.meta.description == meta.description && entry.meta.purpose == meta.purpose);
    CHECK(entry.meta.focusMetrics.size() == 2 && entry.meta.focusMetrics[1] == "店舗別の売上");
    CHECK(!entry.meta.createdAt.empty() && !entry.meta.gameBuild.empty());
    CHECK(entry.fingerprint != 0 && entry.sizeBytes > 0);
    // 街の部分の指紋は、保存前のデータの指紋と同じ(メタ情報は、含まれない)。
    std::uint64_t original = 0;
    CHECK(TestMapStore::ComputeFingerprint(WorldBytes(1), original) && original == entry.fingerprint);
    // ゲームが、そのまま読める(普通のセーブとして、必要な項目が残っている。知らない項目 testMap は、読み込みが無視する)。
    Json parsed; std::string error;
    CHECK(Json::Parse(Slurp(root / "area_basic.json"), parsed, error) && parsed.Has("version") && parsed.Has("roads") && parsed.Has("testMap"));

    // 上書きしない(同じ名前・大文字小文字の違い・別のフォルダ)。元ファイルは、変わらない。
    const std::string before = Slurp(root / "area_basic.json");
    worldMarker = 2;
    CHECK(!store.SaveNew("area_basic", meta, TestMapFolder::Official, message) && message.find("上書きはしません") != std::string::npos);
    CHECK(!store.SaveNew("AREA_BASIC", meta, TestMapFolder::Official, message));
    CHECK(!store.SaveNew("area_basic", meta, TestMapFolder::Draft, message));
    CHECK(Slurp(root / "area_basic.json") == before);
    CHECK(CountJson(root) == 1 && CountJson(root / "Drafts") == 0);

    // 不正な名前は、何も作らない。
    for (const char* bad : { "../x", "a/b", "a\\b", "x.json", "テスト", "CON", "", "Drafts" })
    {
        CHECK(!store.SaveNew(bad, meta, TestMapFolder::Official, message));
    }
    CHECK(CountJson(root) == 1 && CountJson(root.parent_path()) == 0);   // TestMaps の外にも、何も作られていない

    // 下書き。
    TestMapMeta draftMeta; draftMeta.name = "試作A";
    CHECK(store.SaveNew("draft_a", draftMeta, TestMapFolder::Draft, message));
    CHECK(fs::exists(root / "Drafts" / "draft_a.json"));
    CHECK(store.Find("draft_a", entry) && entry.folder == TestMapFolder::Draft && entry.meta.name == "試作A");
    std::uint64_t marker2 = 0;
    CHECK(TestMapStore::ComputeFingerprint(WorldBytes(2), marker2) && entry.fingerprint == marker2 && marker2 != original);   // 街が違えば、指紋も違う

    // 一覧: 正式 → 下書き。
    std::vector<TestMapEntry> list = store.List();
    CHECK(list.size() == 2 && list[0].id == "area_basic" && list[1].id == "draft_a");

    // 名前なしで保存した時は、id が表示名になる。
    CHECK(store.SaveNew("plain", TestMapMeta{}, TestMapFolder::Draft, message));
    CHECK(store.Find("plain", entry) && entry.meta.name == "plain");

    // メタ情報の更新: 街の部分(指紋)は変わらない。作成日時・ビルドは、元の値のまま。
    TestMapEntry beforeUpdate;
    CHECK(store.Find("area_basic", beforeUpdate));
    TestMapMeta edited = beforeUpdate.meta;
    edited.description = "説明を直した";
    edited.name = "エリア基本形(改)";
    edited.createdAt = "1999-01-01 00:00:00";      // 渡しても、無視される
    CHECK(store.UpdateMeta("area_basic", edited, message));
    TestMapEntry afterUpdate;
    CHECK(store.Find("area_basic", afterUpdate));
    CHECK(afterUpdate.meta.description == "説明を直した" && afterUpdate.meta.name == "エリア基本形(改)");
    CHECK(afterUpdate.fingerprint == beforeUpdate.fingerprint);
    CHECK(afterUpdate.meta.createdAt == beforeUpdate.meta.createdAt && afterUpdate.meta.gameBuild == beforeUpdate.meta.gameBuild);

    // 読み込み: 街の入出力に、正しいパスが渡る。使用中の情報が入る。失敗したら入らない。
    CHECK(store.Load("area_basic", message));
    CHECK(loadedPaths.size() == 1 && fs::path(loadedPaths[0]).filename() == "area_basic.json");
    CHECK(store.Active().valid && store.Active().id == "area_basic" && store.Active().fingerprint == original);
    store.ClearActive();
    loadOk = false;
    CHECK(!store.Load("draft_a", message));
    CHECK(!store.Active().valid);
    loadOk = true;
    CHECK(!store.Load("missing", message));

    // 名前変更: ファイルが移り、衝突は断る。使用中の id も追従する。
    CHECK(store.Load("area_basic", message));
    CHECK(!store.Rename("area_basic", "draft_a", message));          // 衝突
    CHECK(!store.Rename("area_basic", "../x", message));              // 不正
    CHECK(!store.Rename("missing", "new_name", message));
    CHECK(store.Rename("area_basic", "area_main", message));
    CHECK(!fs::exists(root / "area_basic.json") && fs::exists(root / "area_main.json"));
    CHECK(store.Active().valid && store.Active().id == "area_main");
    CHECK(store.Find("area_main", entry) && entry.fingerprint == original);                 // 中身は、変わらない

    // 正式 <-> 下書き。
    CHECK(store.Move("draft_a", TestMapFolder::Official, message));
    CHECK(fs::exists(root / "draft_a.json") && !fs::exists(root / "Drafts" / "draft_a.json"));
    CHECK(store.Move("draft_a", TestMapFolder::Draft, message));
    CHECK(fs::exists(root / "Drafts" / "draft_a.json"));
    Spit(root / "draft_a.json", WorldBytes(3));                                            // 移す先に、同じ名前があると、断る
    CHECK(!store.Move("draft_a", TestMapFolder::Official, message) && message.find("両方") != std::string::npos);     // どちらか分からない
    CHECK(!store.Delete("draft_a", message) && !store.Rename("draft_a", "other_x", message) && !store.Load("draft_a", message));   // 曖昧な時は、何もしない
    CHECK(fs::exists(root / "draft_a.json") && fs::exists(root / "Drafts" / "draft_a.json"));
    fs::remove(root / "draft_a.json");

    // 壊れたファイル・関係ないファイル。
    Spit(root / "broken.json", "{ this is not json");
    Spit(root / "notmap.json", "{\"hello\": 1}");
    Spit(root / "memo.txt", "hello");
    Spit(root / "stale.json.tmp", "{}");
    list = store.List();
    int invalid = 0, found = 0;
    for (const TestMapEntry& e : list)
    {
        if (e.id == "broken" || e.id == "notmap") { ++found; if (!e.valid && !e.error.empty()) { ++invalid; } }
        CHECK(e.id != "memo" && e.id != "stale.json");
    }
    CHECK(found == 2 && invalid == 2);
    CHECK(!store.Load("broken", message) && !store.UpdateMeta("broken", edited, message));
    CHECK(store.Find("broken", entry) && !entry.valid);

    // 削除: そのファイルだけ。使用中なら、使用中の情報も消える。
    const std::string keep = Slurp(root / "Drafts" / "draft_a.json");
    CHECK(store.Delete("area_main", message));
    CHECK(!fs::exists(root / "area_main.json") && fs::exists(root / "Drafts" / "draft_a.json") && Slurp(root / "Drafts" / "draft_a.json") == keep);
    CHECK(!store.Active().valid);
    CHECK(!store.Delete("area_main", message));

    // 街を保存できない時(入出力の失敗)は、ファイルを作らない。
    TestMapWorldIO failing;
    failing.capture = [](std::string&, std::string& m) { m = "保存できない"; return false; };
    store.SetWorldIO(failing);
    const int count = CountJson(root);
    CHECK(!store.SaveNew("will_fail", meta, TestMapFolder::Official, message) && CountJson(root) == count && !fs::exists(root / "will_fail.json"));
    CHECK(!store.SaveNew("will_fail", meta, TestMapFolder::Official, message));
}

// ---- 配置の指紋(時間で変わる状態は含めない) ----
static std::string LayoutWorld(int roads, int buildings, int clockMinute, float reputation, bool published)
{
    Json root = Json::MakeObject();
    root.Set("version", 1);
    root.Set("cellSize", 0.5);
    Json r = Json::MakeArray();
    for (int i = 0; i < roads; ++i) { Json road = Json::MakeObject(); road.Set("width", 1); road.Set("id", i); r.Push(std::move(road)); }
    root.Set("roads", std::move(r));
    Json b = Json::MakeArray();
    for (int i = 0; i < buildings; ++i) { Json bld = Json::MakeObject(); bld.Set("type", "Shop"); bld.Set("n", i); b.Push(std::move(bld)); }
    root.Set("buildings", std::move(b));
    Json state = Json::MakeObject();
    Json clock = Json::MakeObject(); clock.Set("minute", clockMinute);
    state.Set("clock", std::move(clock));
    Json areas = Json::MakeObject();
    Json list = Json::MakeArray();
    Json area = Json::MakeObject();
    area.Set("id", 1); area.Set("minX", 0.0); area.Set("minZ", 0.0); area.Set("maxX", 4.0); area.Set("maxZ", 4.0); area.Set("published", published);
    area.Set("reputation", reputation);                      // 時間で変わる
    list.Push(std::move(area));
    areas.Set("areas", std::move(list));
    state.Set("commercialAreas", std::move(areas));
    root.Set("state", std::move(state));
    return root.Dump(true);
}

static void TestLayoutFingerprint()
{
    std::uint64_t base = 0, same = 0, moreBuildings = 0, moreRoads = 0, otherPublished = 0, full1 = 0, full2 = 0;
    CHECK(TestMapStore::ComputeLayoutFingerprint(LayoutWorld(2, 3, 100, 1.0f, true), base));
    // 時間で変わる状態(時計・評判)が違っても、配置は同じ。
    CHECK(TestMapStore::ComputeLayoutFingerprint(LayoutWorld(2, 3, 999, 0.7f, true), same) && same == base);
    // 建物・道路・周知の有無が違えば、配置が違う。
    CHECK(TestMapStore::ComputeLayoutFingerprint(LayoutWorld(2, 4, 100, 1.0f, true), moreBuildings) && moreBuildings != base);
    CHECK(TestMapStore::ComputeLayoutFingerprint(LayoutWorld(3, 3, 100, 1.0f, true), moreRoads) && moreRoads != base);
    CHECK(TestMapStore::ComputeLayoutFingerprint(LayoutWorld(2, 3, 100, 1.0f, false), otherPublished) && otherPublished != base);
    // 街の全体の指紋は、時間で変わる状態も含む(初期状態の同一性)。
    CHECK(TestMapStore::ComputeFingerprint(LayoutWorld(2, 3, 100, 1.0f, true), full1) && TestMapStore::ComputeFingerprint(LayoutWorld(2, 3, 999, 1.0f, true), full2) && full1 != full2);
    std::uint64_t dummy = 0;
    CHECK(!TestMapStore::ComputeLayoutFingerprint("not json", dummy));
}

// ---- 読み込み前の街の控え・元の街に戻す・配置の変更の検出 ----
static void TestSession(const fs::path& sandbox)
{
    const fs::path root = sandbox / "session" / "TestMaps";
    TestMapStore store(root.string());
    TestMapSession session(store);

    std::string world = LayoutWorld(1, 1, 10, 1.0f, true);       // 今の(ユーザーの)街
    bool loadFails = false;
    TestMapWorldIO io;
    io.capture = [&](std::string& bytes, std::string&) { bytes = world; return true; };
    io.loadFile = [&](const std::string& path, std::string& message)
    {
        if (loadFails) { message = "読めない"; return false; }
        // 実際のゲームと同じく、読み込んだ街は、ファイルの中身(メタ情報つき)になる。
        world = Slurp(path);
        message = "ok";
        return true;
    };
    store.SetWorldIO(io);
    TestMapSessionIO sio;
    sio.capture = io.capture;
    sio.loadBytes = [&](const std::string& bytes, const std::string&, std::string&) { world = bytes; return true; };
    session.SetIO(sio);

    std::string message;
    const std::string userCity = world;
    // テストマップを2つ作る(街の配置が違う)。
    world = LayoutWorld(2, 2, 10, 1.0f, true);
    CHECK(store.SaveNew("map_a", TestMapMeta{}, TestMapFolder::Official, message));
    world = LayoutWorld(3, 5, 10, 1.0f, true);
    CHECK(store.SaveNew("map_b", TestMapMeta{}, TestMapFolder::Draft, message));
    world = userCity;
    const std::string fileA = Slurp(root / "map_a.json");
    const std::string fileB = Slurp(root / "Drafts" / "map_b.json");

    // 最初は、控えが無い。使用中でもない。
    CHECK(!session.HasOriginal() && !session.Active().valid);
    bool modified = true;
    CHECK(!session.IsLayoutModified(modified));                   // 使用中でなければ、調べられない

    // 読み込み: 今の街が、控えられる。使用中の情報が入る。
    CHECK(session.Load("map_a", message));
    CHECK(session.HasOriginal() && session.Active().valid && session.Active().id == "map_a");
    CHECK(world != userCity);
    CHECK(session.IsLayoutModified(modified) && !modified);       // 読み込んだ直後は、元と同じ配置
    // 時間が進んだ(時計・評判)だけでは、「変更あり」にならない。
    world = LayoutWorld(2, 2, 555, 0.6f, true);
    CHECK(session.IsLayoutModified(modified) && !modified);
    // 建築した(建物を足した)ら、「変更あり」になる。
    world = LayoutWorld(2, 3, 555, 0.6f, true);
    CHECK(session.IsLayoutModified(modified) && modified);

    // 別のテストマップへ移る: 控えは、最初の街のまま(テストマップ上の変更を、控えない)。
    CHECK(session.Load("map_b", message));
    CHECK(session.Active().id == "map_b");
    // 元の街へ戻す: 最初の街に、そっくり戻る。使用中・控えは、消える。
    CHECK(session.RestoreOriginal(message));
    CHECK(world == userCity);
    CHECK(!session.HasOriginal() && !session.Active().valid);
    CHECK(!session.RestoreOriginal(message));                     // 控えが無ければ、戻せない

    // 元のテストマップのファイルは、一切、変わっていない(読み込み・建築・戻す、の後でも)。
    CHECK(Slurp(root / "map_a.json") == fileA && Slurp(root / "Drafts" / "map_b.json") == fileB);

    // 読み込みに失敗した時: 街は、そのまま。控えも使用中も、作らない。
    loadFails = true;
    world = userCity;
    CHECK(!session.Load("map_a", message));
    CHECK(world == userCity && !session.HasOriginal() && !session.Active().valid);
    loadFails = false;
    CHECK(!session.Load("missing", message) && !session.HasOriginal());

    // 控えを取った後で、読み込みに失敗(2つ目)しても、最初の控えは、そのまま。
    CHECK(session.Load("map_a", message));
    loadFails = true;
    CHECK(!session.Load("map_b", message));
    CHECK(session.HasOriginal() && session.Active().id == "map_a");
    loadFails = false;
    // 戻す時に、街の入れ替えに失敗したら、控えを残す(もう一度、試せる)。
    TestMapSessionIO failingRestore;
    failingRestore.capture = io.capture;
    failingRestore.loadBytes = [](const std::string&, const std::string&, std::string& m) { m = "失敗"; return false; };
    session.SetIO(failingRestore);
    CHECK(!session.RestoreOriginal(message) && session.HasOriginal());
    session.SetIO(sio);
    CHECK(session.RestoreOriginal(message) && world == userCity);

    // 使用中のテストマップを、削除・名前変更した時の、使用中の情報。
    CHECK(session.Load("map_a", message));
    CHECK(store.Rename("map_a", "map_a2", message) && session.Active().id == "map_a2");
    CHECK(store.Delete("map_a2", message) && !session.Active().valid);
    CHECK(session.HasOriginal());                                 // 控えは、残る(元の街へ戻せる)
    CHECK(session.RestoreOriginal(message) && world == userCity);

    // 街の入出力が無い時は、何もせず、断る(落ちない)。
    TestMapSession bare(store);
    CHECK(!bare.Load("map_b", message) && !bare.RestoreOriginal(message));

    // クイックセーブ(F5/F9)の保存先: テストマップ使用中は、普段のクイックセーブとは、別のファイル。使っていなければ、普段のファイル。
    const std::string normalQuickSave = "Saves/quicksave.json";
    CHECK(session.QuickSavePath(normalQuickSave) == normalQuickSave);
    CHECK(session.Load("map_b", message));
    CHECK(session.QuickSavePath(normalQuickSave) == TestMapSession::kQuickSavePath);
    CHECK(session.QuickSavePath(normalQuickSave) != normalQuickSave);
    CHECK(PathGuard::IsInsideDirectory(session.QuickSavePath(normalQuickSave), normalQuickSave) == false);   // 普段のクイックセーブとは、別のファイル
    // 使用をやめる(別のセーブを読み込んだ時・「使用をやめる」): 使用中の表示も、控えも、消える。街は、そのまま。
    const std::string cityBefore = world;
    session.EndSession();
    CHECK(!session.Active().valid && !session.HasOriginal() && world == cityBefore);
    CHECK(session.QuickSavePath(normalQuickSave) == normalQuickSave);
    CHECK(!session.RestoreOriginal(message));
}

// ---- 特定のファイルへの保存を止める(普段のクイックセーブを守る): ScenarioManager::SetBlockedSavePath が使う判定 ----
static void TestBlockedFile(const fs::path& sandbox)
{
    const fs::path saves = sandbox / "blocked" / "Saves";
    fs::create_directories(saves);
    const std::string blocked = (saves / "quicksave.json").string();
    auto isBlocked = [&](const std::string& path) { return PathGuard::IsInsideDirectory(path, blocked); };
    CHECK(isBlocked(blocked));
    std::string upper = blocked;
    for (char& c : upper) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    CHECK(isBlocked(upper));
    CHECK(isBlocked((saves / ".." / "Saves" / "quicksave.json").string()));
    std::string slashed = blocked;
    for (char& c : slashed) { if (c == '\\') { c = '/'; } }
    CHECK(isBlocked(slashed));
    CHECK(isBlocked(blocked + " "));                                          // 末尾の空白(Windowsは、同じファイルとして扱う)
    CHECK(isBlocked(blocked + "."));
    // 同じフォルダの、別のファイルは、止めない(テスト用のクイックセーブ・普通のセーブ)。
    CHECK(!isBlocked((saves / "quicksave_testmap.json").string()));
    CHECK(!isBlocked((saves / "quicksave.json.bak").string()));
    CHECK(!isBlocked((saves / "my_save.json").string()));
}

int main()
{
    const fs::path sandbox = fs::temp_directory_path() / ("GAME_testmap_test_" + std::to_string(::GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox);

    TestIds();
    TestPathGuard(sandbox);
    TestAtomicFile(sandbox);
    TestStore(sandbox);
    TestLayoutFingerprint();
    TestSession(sandbox);
    TestBlockedFile(sandbox);

    fs::remove_all(sandbox, ec);
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}

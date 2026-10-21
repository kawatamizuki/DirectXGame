#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 素材フォルダの中にある、読めるモデル(OBJ/FBXなど。ModelLoaderRegistryが対応している物)1つぶんの情報。
struct AssetEntry
{
    std::string path;       // プロジェクトの作業フォルダからの相対パス(区切りは「/」)。絶対パスは持たない
    std::string name;       // 表示名(拡張子を除いたファイル名)
    std::string pack;       // 素材パック名(同じ配布物のまとまり。例: "CityKitCommercial"、"Assets/Incoming/SimplePolyCity")
    std::string extension;  // 小文字・ドット付き(例: ".obj")
    uint64_t fileSize = 0;  // バイト数(サムネイルの更新判定に使う)
    int64_t modifiedTime = 0; // 更新日時(ファイルシステムの刻み。比較にだけ使う)

    // 同じモデルの、より優先の形式(OBJ > FBX)のファイルがある時にtrue(同じパックで、フォルダ名が形式名だけ違う、または同じフォルダ)。
    // Kenneyのパックは、同じモデルが OBJ・FBX・GLB で入っているため、一覧では重複を隠すのに使う。
    bool duplicate = false;

    std::string directory;  // モデルのあるフォルダ(作業フォルダからの相対パス。区切りは「/」)
};

// フォルダの木の1つの節。Assetsパネルの左の木に出す。
struct FolderNode
{
    std::string path;                // 作業フォルダからの相対パス(区切りは「/」)。仮のルート(「すべて」)は空
    std::string name;                // 表示名(最後の階層)
    int parent = -1;                 // 親の添字(仮のルートは-1)
    std::vector<int> children;       // 子の添字(名前順)
    int modelCount = 0;              // このフォルダの下(サブフォルダ含む)にあるモデルの数(重複を除く)
};

// フォルダの操作(作成・名前の変更・移動)の途中で、「使用中なので触れない」を判定するための関数。
// 例: シーンに置いたPropが、そのフォルダのモデルを使っている時にtrueを返す。trueなら操作を止める。
using FolderLockCheck = std::function<bool(const std::string& folderPath)>;

// 探す場所の指定。
struct AssetRoot
{
    std::string path;       // 作業フォルダからの相対パス(例: "Assets/Models")
    int packDepth = 1;      // モデルの相対パスの先頭から何階層ぶんを「パック名」にするか
                            // (例: "CityKitCommercial/Models/..." は1、"Assets/Models/<パック>/..." は3)
};

// 素材フォルダを再帰的に探して、読めるモデルの一覧を作る。ファイルを読み込まず、見つけるだけなので軽い。
// 形式の判定はModelLoaderRegistry(拡張子)に任せるので、新しい形式のローダーを足せば、自動で一覧に出る。
class AssetCatalog
{
public:
    // 標準の探す場所: Assets/Models、Assets/Incoming(新しい素材の置き場)と、既存の素材フォルダ。
    static std::vector<AssetRoot> DefaultRoots();

    void SetRoots(const std::vector<AssetRoot>& roots) { m_roots = roots; }

    // 探し直して、一覧を作り直す。存在しないフォルダは飛ばす。ファイルの名前順(パック → パス)に並べる。
    void Scan();

    // 全ての項目(重複も含む)。
    const std::vector<AssetEntry>& GetEntries() const { return m_entries; }

    // パック名の一覧(重複なし、名前順)。
    const std::vector<std::string>& GetPacks() const { return m_packs; }

    // 重複を隠した時に見える項目の数。
    size_t CountVisible() const;

    // ---- フォルダの木 ----

    // 木の全ての節。添字0は仮のルート(path が空。「すべて」)。Scan() のたびに作り直す。
    // 含まれるのは、モデルがあるフォルダとその親、および Assets/Models・Assets/Incoming の下の全てのフォルダ(空でも)。
    const std::vector<FolderNode>& GetFolders() const { return m_folders; }

    // pathの節の添字。無ければ-1。空文字(ルート)は0。
    int FindFolder(const std::string& path) const;

    // entryが、folder の中にあるか。includeSubfolders が true ならサブフォルダの中も含む。folder が空なら、全て。
    static bool IsInFolder(const AssetEntry& entry, const std::string& folder, bool includeSubfolders);

    // ---- フォルダの整理(実行中にUnityのように。Assets/Models と Assets/Incoming の下だけ) ----
    // 個々のモデルファイルの移動は行わない(MTLやテクスチャが他のモデルと共有されていて、壊れるため)。削除もしない。
    // どれも、成功したら true で、結果の新しいパスを outPath に入れる。失敗したら false で、理由を error に入れる。
    // 操作の後は Scan() を呼んで、木を作り直すこと。

    // folder の下に、新しいフォルダを作れるか(Assets/Models、Assets/Incoming、およびその下)。
    static bool CanCreateIn(const std::string& folder);
    // folder 自体の名前の変更・移動ができるか(Assets/Models・Assets/Incoming の「下」のフォルダだけ)。
    static bool CanModify(const std::string& folder);
    // フォルダ名として使えるか(英数字・「_」「-」・スペースだけ。空・長すぎ・Windowsの予約名は不可)。理由は error へ。
    static bool IsValidFolderName(const std::string& name, std::string& error);

    static bool CreateFolder(const std::string& parentFolder, const std::string& name, std::string& outPath, std::string& error);
    static bool RenameFolder(const std::string& folder, const std::string& newName, std::string& outPath, std::string& error,
        const FolderLockCheck& isLocked = FolderLockCheck());
    static bool MoveFolder(const std::string& folder, const std::string& newParentFolder, std::string& outPath, std::string& error,
        const FolderLockCheck& isLocked = FolderLockCheck());

    // pack(パックのフォルダ)の中、またはモデルのフォルダから親へさかのぼって、License.txt などの
    // ライセンス文書を探し、見つかればその相対パスを返す(無ければ空)。
    static std::string FindLicenseFile(const AssetEntry& entry);

private:
    std::vector<AssetRoot> m_roots;
    std::vector<AssetEntry> m_entries;
    std::vector<std::string> m_packs;
    std::vector<FolderNode> m_folders;

    // 木を作り直す(Scan の最後に呼ぶ)。
    void BuildFolderTree();
};

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// エクスプローラーからゲームの画面へドロップされたファイル・フォルダを、プロジェクトの Assets/Incoming/ へ**コピー**して取り込む。
// (元の場所のファイルは動かさない・消さない。取り込んだ物は、Assetsパネルで見て、使う物だけ Assets/Models へ移す。)
//
// 画面・OS・ImGui に依存しない部分(コピーの規則)だけを持つので、単体テストできる。
// ドロップの受け取り(WM_DROPFILES)は Window、取り込み後の表示は AssetBrowser が受け持つ。
//
// 規則:
//  ・フォルダ: Assets/Incoming/<名前>/ へ、中身を再帰的にコピーする。コピーするのは「使う拡張子だけ」
//    (モデル .obj .fbx と .mtl、画像 .png .jpg .jpeg .tga .bmp .dds、説明書き .txt .md .html .pdf)。
//    .blend や .zip など、重くて使わない物は飛ばし、飛ばした数と大きさを報告する。
//  ・ファイル: モデル(.obj/.fbx)を Assets/Incoming/Dropped_<日時>/ へ。OBJは、MTLと、MTLが参照するテクスチャ(相対位置のまま)も
//    一緒に。FBXは、同じフォルダの画像と、同じ場所の Textures フォルダも一緒に。
//  ・同じ名前のフォルダがあれば、_2、_3 …を付ける(上書きしない)。名前は、使える文字(英数字・_・-・スペース)に直す。
//  ・合計が大きすぎる(既定1GB超)物は、コピーせずに理由を報告する。
//  ・ファイルの中の名前(MTL・テクスチャの参照)は変えない。プロジェクトの文字コードで扱えない名前のファイルは、飛ばして報告する。
//  ・ドロップされた物が、既に Assets/Incoming の中にある場合は、取り込まない(自分自身へのコピーの防止)。
struct ImportOptions
{
    std::string incomingRoot = "Assets/Incoming";          // 取り込み先(プロジェクトの作業フォルダからの相対パス)
    uint64_t maxTotalBytes = 1024ull * 1024ull * 1024ull;  // 1回の取り込み(1つのドロップ元)の合計の上限(バイト)
    std::string timestamp;                                 // "Dropped_" の後ろに付ける日時。空なら今の日時(テストで固定するため)
};

struct ImportResult
{
    bool ok = false;                          // 1つでも取り込めたか
    std::string message;                      // 利用者に見せるまとめ(日本語)
    std::vector<std::string> importedFolders; // 作った取り込み先のフォルダ(作業フォルダからの相対パス、「/」区切り)
    std::vector<std::string> warnings;        // 飛ばしたファイル・取り込めなかった理由など
    size_t copiedFiles = 0;                   // コピーしたファイルの数
    size_t copiedModels = 0;                  // そのうち、モデル(.obj/.fbx)の数
    uint64_t copiedBytes = 0;
    size_t skippedFiles = 0;                  // 使わない拡張子などで飛ばした数
    uint64_t skippedBytes = 0;
};

class AssetImporter
{
public:
    // sourcesを取り込む。失敗しても例外は出さず、結果(ok・message・warnings)に入れる。
    static ImportResult Import(const std::vector<std::filesystem::path>& sources, const ImportOptions& options = ImportOptions());

    // コピーする拡張子か(小文字・ドット付き)。
    static bool IsAllowedExtension(const std::string& lowerExtension);
    // モデルの拡張子か(.obj / .fbx)。
    static bool IsModelExtension(const std::string& lowerExtension);

    // フォルダ名として使える形に直す(使えない文字は _ に。連続する _ は1つに。前後の _ とスペースは削る。空なら "Imported")。
    static std::string SanitizeName(const std::wstring& name);

    // parentの下で、まだ無い名前を返す(name、name_2、name_3 …)。
    static std::string MakeUniqueName(const std::filesystem::path& parent, const std::string& name);

    // パスの全ての文字が、プロジェクトが使う文字コード(システムの文字コード)で表せるか。
    // 表せないと、ローダー(std::stringのパス)が開けない。
    static bool IsRepresentableInAnsi(const std::filesystem::path& path);
};

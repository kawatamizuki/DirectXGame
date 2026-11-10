#pragma once
#include <string>

// ファイルのパスが、保護したフォルダの中を指すかの判定(画面・ゲームに依存しない)。
// 通常のセーブ処理が、テストマップの元ファイルを上書きしないための門番。パスの書き方を変えて、すり抜けられないようにする:
//  ・相対パス・絶対パス・「./」「../」・区切りの「\」と「/」・大文字小文字の違い
//  ・既に存在する親フォルダは、ファイルシステムの同一性(std::filesystem::equivalent)で比べる(短い名前・ジャンクション・末尾のドット/空白も同じ物と判る)
//  ・安全側に倒す: 判定できない・怪しい書き方(「:」でファイルの別の流れを指す、デバイス名の接頭辞など)は、「保護フォルダの中」として扱う(書かせない)
namespace PathGuard
{
    // path が、directory の中(直下・下位のどこでも)を指すか。directory 自身を指す場合も true。
    // directory が、まだ存在しなくても、パスの文字の比較(正規化後・大文字小文字を区別しない)で判定する。
    bool IsInsideDirectory(const std::string& path, const std::string& directory);

    // IsInsideDirectory の本体(例外を投げることがある。直接は呼ばない)。
    bool IsInsideDirectoryImpl(const std::string& path, const std::string& directory);
}

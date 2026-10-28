#pragma once
#include <cstddef>
#include <string>
#include <vector>

// 音のパック(Sounds/sounds.pak): 複数の音のファイル(mp3・wav)を、1つのファイルにまとめたもの。
//
// なぜ作るか: BGMなどの素材には、「音源のファイルを、そのままの形(mp3)で、利用者が取り出して複製できる状態で配ってはいけない」
// という利用条件がある(THIRD_PARTY_LICENSES.md)。配布する実行環境には、mp3をそのまま入れず、このパックにして入れる。
// ゲームは、起動時にパックを読んで、中の音を、メモリの上で(ファイルとしては取り出せない形で)鳴らす。
//
// 中身は、元のファイルのバイト列を、名前ごとに違う鍵でかき混ぜた(Scramble)だけ。暗号としての強さは無く、
// 「ダブルクリックでmp3として開ける・コピーできる」状態を避けるためのもの(「容易に取り出せない」の条件のため)。
// 開発中(Sounds/にmp3がある時)は、パックが無くても、普通のファイルから鳴らせる。
//
// ファイルの形式(数値は全て、リトルエンディアン):
//   "GSPK"(4バイト) / 版(u32=1) / 個数(u32)
//   個数ぶん: 名前の長さ(u16) / 名前(UTF-8。sounds.jsonのfileと同じ文字列) / 大きさ(u32) / 元のデータのハッシュ(u32、FNV-1a)
//   その後ろに、各ファイルの、かき混ぜたバイト列を、順番に並べる
struct SoundPackEntry
{
    std::string name;                       // 音の表(sounds.json)の file と同じ文字列
    std::vector<unsigned char> data;        // 元のファイルのバイト列(mp3・wavそのもの)
};

class SoundPack
{
public:
    static constexpr unsigned int kVersion = 1;
    static constexpr size_t kMaxEntries = 4096;
    static constexpr size_t kMaxNameLength = 512;

    // バイト列を、名前から決まる鍵で、かき混ぜる。同じ関数を、もう一度かけると、元に戻る。
    // (tools/pack_sounds.ps1 の中の同じ手順と、完全に一致させること)
    static void Scramble(const std::string& name, unsigned char* data, size_t size);

    // FNV-1a(32bit)。名前の鍵・データの確認(ハッシュ)に使う。
    static unsigned int Hash(const unsigned char* data, size_t size);

    // パックを作る(テスト・ツール用)。一時ファイルに書いてから置き換える。
    static bool Write(const std::string& path, const std::vector<SoundPackEntry>& entries, std::string& message);

    // パックを読む。ファイルが開けない・形式が壊れている(途中で切れている・大きさが合わない等)時はfalse(outは空)。
    // 中身が壊れた(ハッシュが合わない)ファイルは、飛ばして、messageに書く(残りは読む)。
    static bool Read(const std::string& path, std::vector<SoundPackEntry>& out, std::string& message);
};

#pragma once
#include <string>

// ファイルを、安全に書く(画面・ゲームに依存しない)。
// 一時ファイル(path + ".tmp")に、書き切ってから、置き換える。書き込み中に落ちても、前のファイルは壊れない。
// 親フォルダが無ければ、作る。失敗したら、errorに理由(日本語)を入れる。一時ファイルは、失敗時に消す。
bool WriteFileAtomic(const std::string& path, const std::string& bytes, std::string& error);

// ファイルを、全部、読む(バイナリ)。失敗したらfalse。
bool ReadFileBytes(const std::string& path, std::string& bytes, std::string& error);

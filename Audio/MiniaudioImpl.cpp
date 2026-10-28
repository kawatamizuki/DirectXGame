// miniaudioの実装(中身)を、プロジェクトの中で、このファイル1か所だけに取り込む。
// (miniaudioは、単一のヘッダーで、ここでMINIAUDIO_IMPLEMENTATIONを定義した時にだけ、実装が展開される)
// 使わない機能(音の書き出し・波形の生成)は、切って、ビルドを軽くする。wav・mp3・flacの読み込みと、再生は、残す。
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION

// 外部のコードのため、警告を止めて読み込む。
#pragma warning(push, 0)
#include "miniaudio.h"
#pragma warning(pop)

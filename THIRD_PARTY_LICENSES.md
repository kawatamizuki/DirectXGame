# 素材・ライブラリのライセンスとクレジット

このプロジェクトが使っている(または使う予定の)外部の素材・ライブラリの出所とライセンスの一覧です。
**新しい素材やライブラリを入れる時は、入れる前にこのファイルへ追記します**(末尾の「追加する時のチェックリスト」参照)。

最終更新: 2026-10-29
※ 内容は取得元のページ・同梱のライセンス文書で確認した範囲です。法的な助言ではありません。提出・配布の前に、各ライセンスの原文を確認してください。

---

## 1. 3Dモデル・テクスチャ(素材)

### 使用中

| 場所 | 名前 | 作者 | ライセンス | 取得元 | 確認状況 |
|---|---|---|---|---|---|
| `CityKitCommercial/` | City Kit (Commercial) 2.1 | Kenney | CC0 1.0 | https://kenney.nl/assets/city-kit-commercial | 同梱の `License.txt` で確認済み |
| `CityKitSuburban/` | City Kit (Suburban) 2.0 | Kenney | CC0 1.0 | https://kenney.nl/assets/city-kit-suburban | 同梱の `License.txt` で確認済み |
| `RoadTiles/` | 3D Road Tiles | Kenney Vleugels(Kenney) | CC0 1.0 | https://kenney.nl/assets/3d-road-tiles | 同梱の `License.txt` で確認済み。地面の見た目(`roadTile_163.obj`)に使用 |
| `Models/Models/`、`Models/Textures/`、`Models/character-female-f.*` | Mini Characters 1.0(2024年公開) | Kenney | CC0 1.0 | https://kenney.nl/assets/mini-characters | パック名はユーザー(制作者)の記憶による。ページ(2026-10-20 確認)で CC0・v1.0 を確認。同梱のライセンス文書は無い。住民の仮モデルに使用 |
| `Models/RoadModels/` | City Kit (Roads) と推定(ページ上の最新版は 2.2) | Kenney | CC0 1.0 | https://kenney.nl/assets/city-kit-roads | **推定**: ファイル名(`road-*`、`electricity-pole*`、`dumpster` など)と一致。取得したバージョンは未確認。同梱のライセンス文書は無い。ページ(2026-10-20 確認)で CC0 を確認 |
| `Models/cube.obj` | 単純な立方体 | 不明 | 不明 | - | **要確認**: 出所の記録なし。自作の単純な形状なら「自作」に変更する。今はどこでも使っていない(Shop の仮モデルから `building-d` に変更済み) |

| `Assets/Models/Stores/convenience/`(コンビニ) | "Mini Market" | Elbolillo (https://sketchfab.com/Elbolilloduro) | **CC BY 4.0** | https://sketchfab.com/3d-models/mini-market-725af75672704fc599a692d64e946475 (2021-04-15 公開) | **2026-10-29 に確認**: ファイル内のメタデータと、Sketchfab の公開API(`api.sketchfab.com/v3/models/<id>`)のライセンス欄が、どちらも CC BY 4.0(作者の表示が必要・商用可)。説明文に追加の制限は無い。Sketchfab のダウンロード可(downloadable)。GLB から変換して使用(下記) |
| `Assets/Models/Stores/supermarket/`(スーパー) | "Lowpoly Supermarket" | Klieg3D (https://sketchfab.com/Klieg3D) | **CC BY 4.0** | https://sketchfab.com/3d-models/lowpoly-supermarket-80f32e7d620147dda44445f79bba4281 (2020-09-13 公開) | 同上(2026-10-29 に確認)。元は、道路・駐車場・車を含む敷地全体。店舗と前の歩道だけを切り出して使用 |
| `Assets/Models/Stores/cinema/`(映画館) | "Cinema" | TRYFIELD (https://sketchfab.com/tryfield) | **CC BY 4.0** | https://sketchfab.com/3d-models/cinema-c74081d273324d409cb2350a2a349a80 (2020-10-18 公開) | 同上(2026-10-29 に確認)。元に付いていた巨大な地面を削除し、壁を両面描画にして使用 |

| `Assets/Models/Stores/cafe/`(カフェ。**手元のみ。配布物には入れない**) | "Coffee Store" | w-murderface (https://www.cgtrader.com/designers/w-murderface) | CGTrader の **Royalty Free License (no AI)**(**無料**ダウンロード) | https://www.cgtrader.com/free-3d-models/exterior/cityscape/coffee-store (Model ID #2129997、公開日 2019-10-17) | **2026-10-29 に CGTrader のページと規約(21A)で確認**: ゲームへの組み込みは可(素材が独自形式で、ゲームの中で表示される形であること)。**エンドユーザーが元の素材を取り出せないよう、合理的な対策を取る**条件がある・元の形での再配布(譲渡)は不可・AI学習への利用は不可。クレジットはライセンス上は必須でないが、ダウンロードページで作者へのクレジットが求められているので、**w-murderface** をクレジットに入れる。FBX(材質入り)から変換して使用。元に付いていた600x600の地面を削除。**配布物には、このモデルを入れない**(`tools/make_release_package.ps1` が、同じ場所に、Kenney(CC0)の建物を置く。手元のモデルをそのまま入れる(読めるOBJ・PNG)と、条件に合わない)。GitHubにも、OBJ・MTLは入れない(`.gitignore`) |

Kenney の素材は、公式サイトの方針として CC0(パブリックドメイン)で配布されており、個人・教育・商用に使えます。

#### CC BY 4.0 の素材(Sketchfab の3つ: コンビニ・スーパー・映画館)の条件
- **作者の表示が必須**(下記「クレジット表記」と、配布物の README.txt、各フォルダの `CREDITS.txt` に書いた)。**ライセンス(CC BY 4.0)へのリンクを付ける**。
- **加工した場合は、加工した旨を表示する**。これらは、GLB → OBJ の変換、頂点への変形の焼き込み、材質を1枚のテクスチャ(アトラス)に統合、テクスチャの縮小(繰り返す模様は、平均の色に置き換え)、一部の切り出し・地面の削除をしている(`tools/convert_model.py`、`tools/convert_store_models.ps1`。各 `CREDITS.txt` に記載)。
- 追加の制限(DRM・再配布を妨げる技術的な制限など)を、素材そのものに付けてはいけない。変換後のファイルは、元のライセンス(CC BY 4.0)のまま。
- 商用利用・再配布・改変は、表示を守れば可。AI学習の禁止などの条項は、無い(ライセンス欄は、表示のみを求めている)。
- **Git の扱い**: 元の GLB は、`.gitignore`(`*.glb`)で、入れない。変換後の OBJ・MTL も、他のモデルと同じく `.gitignore`(`*.obj`、`*.mtl`)の対象。アトラスの PNG と `CREDITS.txt` は、入る。再現は `tools/convert_store_models.ps1`。
- 注意: 元のモデルの質感(特に、コンビニの壁の暗さ)は、元のまま。見た目の確認は、docs/study/glb_conversion.md。
クレジットは必須ではありませんが、**「Kenney (www.kenney.nl)」** と書くことを推奨されているので、下記「クレジット表記」に入れています。

### 音声(BGM・効果音)

| 場所 | 用途 | 元のファイル名 | 作者・取得元 | ライセンス・利用条件 | 確認状況 |
|---|---|---|---|---|---|
| `Sounds/bgm_title.mp3` | タイトル画面のBGM | `青空空港.mp3` | 「青空空港」 作曲: かずち(OpenTracks 旧 DOVA-SYNDROME。2026-09-15 に名称変更) https://opentracks.com/bgm/detail/3955 (公開 2015-12-23)。作曲者: https://opentracks.com/creator/detail/50 | 同サイトの「音源利用ライセンス」(下記)。作曲者の追加条件は「成人向けの表現がある作品でも使ってよい」のみで、制限を足すものではない(2026-10-28 に確認) | 確認済み。ゲームでの背景音楽としての利用は可。**配布用 zip には mp3 を入れず、専用のパック(`Sounds/sounds.pak`)に入れて同梱する**(下記。`tools/pack_sounds.ps1`、設計は docs/design/audio.md §14)。 |
| `Sounds/ui_open.mp3` | UIを開く時の効果音 | `決定ボタンを押す32.mp3` | 効果音ラボ https://soundeffect-lab.info/sound/button/ | 同サイトの利用規約 https://soundeffect-lab.info/agreement/(2026-10-28 に確認) | 確認済み。このゲームでの使い方(操作音として組み込む)は許可されている。下記 |

#### 効果音ラボの規約(2026-10-28 に確認した要約)
- 商用を含め無料。報告・リンク・クレジット表記は不要(任意)。ただし、アダルト・公序良俗に反する作品・違法行為への利用は禁止。AI学習用のデータにすることは禁止。
- **再配布は禁止**。「アプリに操作音として効果音を組み込む(音源ファイルむき出しでも可)」は、**再配布に当たらない**と明記されている。このゲームは当てはまる。
- 効果音を鳴らすことが主目的のアプリ(ポン出しサンプラー等)、効果音そのものの配布、効果音だけを流す動画は、再配布に当たる(やらない)。
- → **公開リポジトリ(GitHub)に mp3 を入れるのは、再配布に当たるので禁止**。`.gitignore` で除外している(そのまま)。ゲーム(配布用 zip)への同梱は、操作音としてなら可。

#### OpenTracks の「音源利用ライセンス」(2026-10-28 に確認した要約。https://opentracks.com/help/articles/license/ )
- 商用・個人を問わず無料。著作権表示・提供表示は不要。**ゲーム(有償・無償の製品・作品)での背景音楽としての利用は可**。加工(形式変換・フェード・ループ化など)も可。AI学習への利用は禁止。
- **禁止事項(このゲームに関係するもの)**:
  - 音源を配布・販売すること。オープン・クローズドを問わず、**インターネット上での公開など、エンドユーザーが音源だけを利用できる方法は全て該当**する。→ **公開リポジトリに mp3 を入れるのは禁止**(`.gitignore` で除外している)。
  - **「コンバート等を行わず、エンドユーザーが容易に音源ファイルに音声ファイルとしてアクセス、複製が可能な状態での利用」**。→ 配布用 zip に mp3 をそのまま入れるのは、この条件に当たる。**対応済み**: 配布用には、mp3 を入れず、独自の形式のパック(`Sounds/sounds.pak`)にして入れ、ゲームが起動時に読んで、メモリの上で鳴らす(ファイルとしては取り出せない。`make_release_package.ps1` は、配布物に mp3・wav が残っていたら止まる)。※ パックは、元のデータをかき混ぜただけの簡単な形式で、暗号としての強さは無い(「容易に取り出せない」ための対応)。条件に合うかの最終的な判断は、サイト側の解釈による。
  - 音源を聴かせることが主目的、または再生以外の主な機能が無い作品。ライブ演奏など。
- 作曲者が別の条件を設けている場合は、作曲者の条件が優先される(曲ごとに確認が必要)。「青空空港」の作曲者(かずち)の追加条件は、制限を足すものではなかった。
### 取得済み・導入を検討中(まだプロジェクトには入れていない)

| 名前 | 作者 | ライセンス | 取得元 | 条件・注意 |
|---|---|---|---|---|
| LowPoly Buildings Pack(ダウンロードしたファイル名は `ultimate_textured_building_pack_by_quaternius`、フォルダ名は「Ultimate Textured Building Pack - Dec 2019」) | quaternius | **CC0 1.0** | https://opengameart.org/content/lowpoly-buildings-pack | **2026-10-20 に OpenGameArt のページで確認**: ライセンス欄が CC0、投稿日 2021-06-18、「個人・商用で自由に使える」の記載。クレジットは求められていない(Patreon での支援のお願いのみ)。ダウンロードしたフォルダにライセンス文書は無いので、このページの確認結果が根拠。FBX/OBJ/Blend の形式あり |
| SimplePoly City | TRiMA29 | CGTrader の **Royalty Free License (no AI)**(**無料**ダウンロード) | https://www.cgtrader.com/free-3d-models/exterior/other/simplepoly-city-low-poly-3d-model (Model ID #6462436、公開日 2025-09-22) | **2026-10-20 に CGTrader のページで確認**: 無料、作者 TRiMA29、ライセンス表記は「Royalty Free License (no AI)」。ダウンロードページに「作者にクレジットを」の表示。条件は下記 |

#### SimplePoly City の条件(CGTrader Royalty Free License / Terms & Conditions 21A の要約)
- **使える**: ゲームへの組み込み。ただし素材は「独自形式で、ゲームの中で表示される形」であること。**エンドユーザーが元の素材ファイルに直接触れないよう、合理的な対策を取る**こと。
- **使えない**: ダウンロードした形のままの再配布・譲渡・販売(**GitHub への元ファイルのアップロードも含む**と解釈するのが安全)。
- **AI**: 機械学習・生成AIの学習に使うことは不可(「no AI」)。このプロジェクトでは使っていない。
- クレジットはライセンス上は必須ではないが、ダウンロードページで作者へのクレジットが求められているので、**「TRiMA29」** をクレジット表記に入れる。
- **このプロジェクトでの扱い(方針)**:
  - 元ファイル(FBX・OBJ・PNG)は **Git にコミットしない**(`Incoming/` など、`.gitignore` で除外した場所に置く)。
  - 企業へ提出する時は、元ファイルをそのまま同梱するのではなく、**独自の形式に変換した物だけ**を入れるか、素材を含まない形で提出し、取得方法をREADMEに書く。
  - どちらにするかは、提出方法が決まった時に再検討する。

## 2. ライブラリ(コード)

| 場所 | 名前 | バージョン | ライセンス | 著作権 | 取得元 |
|---|---|---|---|---|---|
| `imgui/` | Dear ImGui(本体と DirectX11/Win32 バックエンド)。**docking ブランチ**(パネルのドッキング用。2026-10-21 に本家 1.92.8 から差し替え) | 1.92.8-docking(公式リポジトリのタグ `v1.92.8-docking`) | MIT | Omar Cornut and contributors | https://github.com/ocornut/imgui |
| `imgui/imstb_truetype.h` | stb_truetype(Dear ImGui 同梱の改変版) | 1.26 | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `imgui/imstb_rectpack.h` | stb_rect_pack(Dear ImGui 同梱) | 1.01 | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `imgui/imstb_textedit.h` | stb_textedit(Dear ImGui 同梱) | - | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `External/DirectXTK/` | DirectX Tool Kit の WICTextureLoader11 のみ | - | MIT | Microsoft Corporation | https://github.com/microsoft/DirectXTK |
| `External/ufbx/` | ufbx(`ufbx.h`、`ufbx.c`、`LICENSE`) | **v0.23.1**(コミット `26a482ae66871d7de36eb722aa060bce95bce274`) | **MIT と Unlicense(パブリックドメイン)のデュアルライセンス。選択: Unlicense**(帰属表示は不要)。念のため `LICENSE` ファイルは同梱している | Samuli Raivio | https://github.com/ufbx/ufbx(2026-10-20 に取得) |
| `External/miniaudio/` | miniaudio(`miniaudio.h`、`LICENSE.txt`) | **v0.11.25**(2026-03-04 版) | **パブリックドメイン と MIT-0 の選択。選択: パブリックドメイン**(帰属表示は不要)。念のため、ヘッダー末尾のライセンス文を `LICENSE.txt` に抜き出して同梱している | David Reid | https://github.com/mackron/miniaudio(2026-10-26 に取得) |

- `imgui/LICENSE.txt` に Dear ImGui の MIT ライセンス全文を同梱しています(docking 版への差し替えで、公式のタグから一緒に取得)。**配布物に含める時も、このファイルを添えてください。** docking 版は本家に追従する枝で、マルチビューポート(パネルを別のOSウィンドウにする機能)は使っていません。
- DirectXTK の2ファイルには、先頭に `Copyright (c) Microsoft Corporation. Licensed under the MIT License.` の記載があります。

- ufbx のファイルの SHA-256(改ざん・取り違えの確認用): `ufbx.h` = `f787be52…72895dc`、`ufbx.c` = `4a956c26…bda7e1d`、`LICENSE` = `0dd48eba…f3615391`(先頭8桁と末尾7桁)。
- ufbx は **ビルドに組み込み済みで、`Graphics/FbxModelLoader` が使っている**(静的モデルのみ。設計は [docs/design/model_loading.md](docs/design/model_loading.md))。動作確認として、SimplePoly City の FBX 99ファイルがすべてエラーなく読めることを確認した。
- miniaudio のファイルの SHA-256: `miniaudio.h` = `ac7af4de…cc55c89`(先頭8桁と末尾7桁)。ビルドには `Audio/MiniaudioImpl.cpp` だけが取り込む。設計は [docs/design/audio.md](docs/design/audio.md)。
- 「なぜこのライブラリを選んだか」は [docs/design/external_libraries.md](docs/design/external_libraries.md) にまとめている。

## 3. フォント
- ゲーム内UIの文字は、実行する PC に入っている Windows 標準フォント(メイリオ、游ゴシック、MS ゴシックの順に探す)を実行時に読み込みます。**フォントファイルはプロジェクトに同梱していません**(再配布していないので、フォントのライセンス上の問題は出ません)。
- 専用フォントを `Fonts/UIFont.ttf` に置く場合は、**そのフォントのライセンス(再配布・埋め込みの可否)を確認して、このファイルに追記**します。

## 4. クレジット表記(ゲーム内・READMEに載せる文面)

```
3D models: Kenney (www.kenney.nl) — CC0
Store models (CC BY 4.0, converted/modified for this game; originals on Sketchfab):
  "Mini Market" by Elbolillo — https://sketchfab.com/3d-models/mini-market-725af75672704fc599a692d64e946475
  "Lowpoly Supermarket" by Klieg3D — https://sketchfab.com/3d-models/lowpoly-supermarket-80f32e7d620147dda44445f79bba4281
  "Cinema" by TRYFIELD — https://sketchfab.com/3d-models/cinema-c74081d273324d409cb2350a2a349a80
  License: http://creativecommons.org/licenses/by/4.0/
Low-poly city models: TRiMA29 (SimplePoly City, CGTrader)   ※使う場合
Building models: Quaternius (quaternius.com) — CC0           ※使う場合
Libraries: Dear ImGui (MIT), stb (public domain / MIT), DirectX Tool Kit (MIT), ufbx (Unlicense / public domain), miniaudio (public domain)
Sounds: 効果音ラボ (soundeffect-lab.info) / OpenTracks (opentracks.com)   ※どちらも表記は不要(任意)。「青空空港」作曲: かずち(表記は任意)
```

## 追加する時のチェックリスト
新しい素材・ライブラリを入れる時は、次を**入れる前に**確認して、このファイルに追記する。
1. **取得元のURL**と**作者**、取得した**日付**を記録する(ページが後で消えても分かるように)。
2. **ライセンス名**と、使える範囲(商用・改変・再配布)を、取得元のページか同梱のライセンス文書で確認する。ライセンス文書が付いていない時は、ダウンロードページの表記を確認して、その旨を書く。
3. **再配布の可否**を確認する。「元ファイルのまま再配布は不可」の素材は、`Incoming/` など Git に入れない場所に置く。
4. **クレジットが必要か**(必須・推奨・不要)を確認し、必要なら「4. クレジット表記」に足す。
5. **AI・生成物・クレジット以外の条件**(AI学習の禁止、用途の制限など)を確認する。
6. 使わなくなった素材は、このファイルから削除せず「使用していない」と書いて残す(過去に提出した版との整合のため)。

## 素材の置き場所(アセットブラウザ)
- 新しい素材パックは、まず `Assets/Incoming/<パック名>/` に置く(**Gitに入れない**。`.gitignore` 済み)。F2のアセットブラウザで見て、使うものだけ `Assets/Models/<パック名>/` へ移す。
- **移す前に**、上の「追加する時のチェックリスト」に従って、このファイルに出所・ライセンス・条件を追記する。再配布できない素材(例: SimplePoly City)は、`Assets/Models/` に移しても元ファイルをそのままGitに入れない(`.gitignore` に追加するか、専用形式に変換した物だけを入れる)。
- サムネイルは `Cache/Thumbnails/` に作られる(Git管理外)。素材から作った画像なので、公開する場所に置かない。

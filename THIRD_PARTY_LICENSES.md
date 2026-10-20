# 素材・ライブラリのライセンスとクレジット

このプロジェクトが使っている(または使う予定の)外部の素材・ライブラリの出所とライセンスの一覧です。
**新しい素材やライブラリを入れる時は、入れる前にこのファイルへ追記します**(末尾の「追加する時のチェックリスト」参照)。

最終更新: 2026-10-20
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

Kenney の素材は、公式サイトの方針として CC0(パブリックドメイン)で配布されており、個人・教育・商用に使えます。
クレジットは必須ではありませんが、**「Kenney (www.kenney.nl)」** と書くことを推奨されているので、下記「クレジット表記」に入れています。

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
| `imgui/` | Dear ImGui(本体と DirectX11/Win32 バックエンド) | 1.92.8 | MIT | Omar Cornut and contributors | https://github.com/ocornut/imgui |
| `imgui/imstb_truetype.h` | stb_truetype(Dear ImGui 同梱の改変版) | 1.26 | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `imgui/imstb_rectpack.h` | stb_rect_pack(Dear ImGui 同梱) | 1.01 | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `imgui/imstb_textedit.h` | stb_textedit(Dear ImGui 同梱) | - | パブリックドメイン(MIT とのデュアル) | Sean Barrett | https://github.com/nothings/stb |
| `External/DirectXTK/` | DirectX Tool Kit の WICTextureLoader11 のみ | - | MIT | Microsoft Corporation | https://github.com/microsoft/DirectXTK |
| `External/ufbx/` | ufbx(`ufbx.h`、`ufbx.c`、`LICENSE`) | **v0.23.1**(コミット `26a482ae66871d7de36eb722aa060bce95bce274`) | **MIT と Unlicense(パブリックドメイン)のデュアルライセンス。選択: Unlicense**(帰属表示は不要)。念のため `LICENSE` ファイルは同梱している | Samuli Raivio | https://github.com/ufbx/ufbx(2026-10-20 に取得) |

- `imgui/` には `LICENSE.txt` を同梱していません。**配布物に含める時は、Dear ImGui の MIT ライセンス全文(https://github.com/ocornut/imgui/blob/master/LICENSE.txt)を添えてください。**
- DirectXTK の2ファイルには、先頭に `Copyright (c) Microsoft Corporation. Licensed under the MIT License.` の記載があります。

- ufbx のファイルの SHA-256(改ざん・取り違えの確認用): `ufbx.h` = `f787be52…72895dc`、`ufbx.c` = `4a956c26…bda7e1d`、`LICENSE` = `0dd48eba…f3615391`(先頭8桁と末尾7桁)。
- ufbx は **ビルドに組み込み済みで、`Graphics/FbxModelLoader` が使っている**(静的モデルのみ。設計は [docs/design/model_loading.md](docs/design/model_loading.md))。動作確認として、SimplePoly City の FBX 99ファイルがすべてエラーなく読めることを確認した。
- 「なぜこのライブラリを選んだか」は [docs/design/external_libraries.md](docs/design/external_libraries.md) にまとめている。

## 3. フォント
- ゲーム内UIの文字は、実行する PC に入っている Windows 標準フォント(メイリオ、游ゴシック、MS ゴシックの順に探す)を実行時に読み込みます。**フォントファイルはプロジェクトに同梱していません**(再配布していないので、フォントのライセンス上の問題は出ません)。
- 専用フォントを `Fonts/UIFont.ttf` に置く場合は、**そのフォントのライセンス(再配布・埋め込みの可否)を確認して、このファイルに追記**します。

## 4. クレジット表記(ゲーム内・READMEに載せる文面)

```
3D models: Kenney (www.kenney.nl) — CC0
Low-poly city models: TRiMA29 (SimplePoly City, CGTrader)   ※使う場合
Building models: Quaternius (quaternius.com) — CC0           ※使う場合
Libraries: Dear ImGui (MIT), stb (public domain / MIT), DirectX Tool Kit (MIT), ufbx (Unlicense / public domain)
```

## 追加する時のチェックリスト
新しい素材・ライブラリを入れる時は、次を**入れる前に**確認して、このファイルに追記する。
1. **取得元のURL**と**作者**、取得した**日付**を記録する(ページが後で消えても分かるように)。
2. **ライセンス名**と、使える範囲(商用・改変・再配布)を、取得元のページか同梱のライセンス文書で確認する。ライセンス文書が付いていない時は、ダウンロードページの表記を確認して、その旨を書く。
3. **再配布の可否**を確認する。「元ファイルのまま再配布は不可」の素材は、`Incoming/` など Git に入れない場所に置く。
4. **クレジットが必要か**(必須・推奨・不要)を確認し、必要なら「4. クレジット表記」に足す。
5. **AI・生成物・クレジット以外の条件**(AI学習の禁止、用途の制限など)を確認する。
6. 使わなくなった素材は、このファイルから削除せず「使用していない」と書いて残す(過去に提出した版との整合のため)。

# 提出用の実行環境(ダブルクリックで動くフォルダと zip)を作る。
# 使い方: PowerShell で、プロジェクトのフォルダから
#     .\tools\make_release_package.ps1            (Releaseをビルドしてから、dist\GAME と dist\GAME.zip を作る)
#     .\tools\make_release_package.ps1 -NoBuild   (ビルドは、せずに、今ある x64\Release\GAME.exe を使う)
# 出力: dist\GAME\(フォルダ)、dist\GAME.zip。dist は Git に入れない(.gitignore)。
param(
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- 1. Releaseをビルドする ----
if (-not $NoBuild) {
    $msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { throw "MSBuild が見つかりません: $msbuild" }
    & $msbuild GAME.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m
    if ($LASTEXITCODE -ne 0) { throw "Releaseのビルドに失敗しました" }
}

$exe = Join-Path $root 'x64\Release\GAME.exe'
if (-not (Test-Path $exe)) { throw "GAME.exe がありません: $exe" }

# ---- 2. 出力先を作り直す(中身は、毎回、最初から作る) ----
$dist = Join-Path $root 'dist'
$out = Join-Path $dist 'GAME'
if (Test-Path $out) { [IO.Directory]::Delete($out, $true) }
[IO.Directory]::CreateDirectory($out) | Out-Null

function Copy-Required([string]$relative) {
    $source = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $source)) { throw "必要なファイルがありません: $relative" }
    $target = Join-Path $out $relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

# ---- 3. 実行に必要な物を集める ----
# 実行ファイル(C++のランタイムは、exeに組み込み済み。別のインストールは不要)
Copy-Item -LiteralPath $exe -Destination (Join-Path $out 'GAME.exe') -Force

# シェーダー(実行時に、exeの隣から読む。SimpleShader.hlsl が、Lighting.hlsli を読み込む)
foreach ($name in 'SimpleShader.hlsl', 'DebugLineShader.hlsl', 'UIShader.hlsl', 'Lighting.hlsli') { Copy-Required $name }

# 初期マップ
Copy-Required 'Scenarios\tutorial.json'

# 音(音の表と、音のパック)。BGMなどの素材は、「音源のファイルを、そのままの形で配ってはいけない」条件があるので、
# mp3・wavは入れず、tools\pack_sounds.ps1 で作るパック(Sounds\sounds.pak)に入れる。
Copy-Required 'Sounds\sounds.json'
& (Join-Path $PSScriptRoot 'pack_sounds.ps1') -Output (Join-Path $out 'Sounds\sounds.pak')
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "音のパックの作成に失敗しました" }

# 使うモデルと、そのテクスチャ(コード側のパスと、同じ相対パスで置く)
$models = @(
    'CityKitSuburban\Models\OBJ format\building-type-a',
    'CityKitCommercial\Models\OBJ format\building-a',
    'CityKitCommercial\Models\OBJ format\building-d',
    'CityKitCommercial\Models\OBJ format\building-e',     # スーパー(暫定の見た目)
    'CityKitCommercial\Models\OBJ format\building-i',     # 映画館(暫定の見た目)
    'CityKitCommercial\Models\OBJ format\building-j',     # カフェ(暫定の見た目)
    'Models\character-female-f',
    'RoadTiles\Models\roadTile_163'
)
foreach ($m in $models) {
    Copy-Required "$m.obj"
    Copy-Required "$m.mtl"
}
foreach ($t in 'CityKitSuburban\Models\OBJ format\Textures\colormap.png',
               'CityKitCommercial\Models\OBJ format\Textures\colormap.png',
               'Models\Textures\colormap.png') {
    Copy-Required $t
}

# ライセンス(使った素材・ライブラリの表記)
foreach ($l in 'THIRD_PARTY_LICENSES.md', 'CityKitSuburban\License.txt', 'CityKitCommercial\License.txt', 'RoadTiles\License.txt') {
    Copy-Required $l
}

# ---- 4. 説明(README.txt) ----
$readme = @'
街づくりゲーム
================

■ 起動
  GAME.exe をダブルクリックしてください。インストールは不要です。
  フォルダごと、好きな場所に置いて構いません(書き込める場所に置いてください。セーブが Saves フォルダに作られます)。

■ 動作環境
  Windows 10 / 11 (64bit)、DirectX 11 に対応したGPU

■ 遊び方
  タイトル画面の「スタート」(または Enter キー)で始まります。
  右下の「メニュー」(または Esc キー)を押すと、時間が止まり、建築・情報・研究・システム・オプション(音量)が出ます。
  建築で、建物や道路を選ぶと、メニューが閉じて、置けるようになります。

■ 操作
  カメラの移動      W A S D / 矢印キー(Shift で速く)、または、マウスの中ボタンのドラッグ
  ズーム            マウスのホイール
  建物・道路を置く    左クリック(道路は、2か所をクリック。自由描きは、ドラッグして、クリックかEnterで確定)
  建物の向きを回す    R キー
  配置をやめる        右クリック / Esc キー
  セーブ / ロード     メニューのシステム、または、F5 / F9

■ Windows の警告が出た場合
  「WindowsによってPCが保護されました」と出た時は、「詳細情報」→「実行」を選んでください。
  (署名の無いアプリのため、出ることがあります)

■ フォルダの中身
  GAME.exe          ゲーム本体
  *.hlsl, *.hlsli   描画のシェーダー(消さないでください)
  CityKit* / Models / RoadTiles   建物・住民・地面のモデル(消さないでください)
  Scenarios         最初の街のデータ
  Sounds            BGM・効果音(sounds.pak にまとめてあります)と、音の一覧(sounds.json)
  THIRD_PARTY_LICENSES.md、各 License.txt   使用した素材・ライブラリのライセンス
'@
[IO.File]::WriteAllText((Join-Path $out 'README.txt'), $readme, (New-Object System.Text.UTF8Encoding($true)))

# ---- 4b. 安全確認: 音源のファイルが、そのままの形で、配布物に入っていないこと ----
$rawAudio = Get-ChildItem -Path $out -Recurse -File | Where-Object { $_.Extension -in '.mp3', '.wav', '.ogg', '.flac' }
if ($rawAudio) {
    throw ("配布物に、音源のファイルがそのまま入っています(素材の規約で禁止): " + (($rawAudio | ForEach-Object { $_.FullName }) -join ', '))
}
# ---- 5. zip にする ----
$zip = Join-Path $dist 'GAME.zip'
if (Test-Path $zip) { [IO.File]::Delete($zip) }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -CompressionLevel Optimal

# ---- 6. 結果 ----
$files = Get-ChildItem -Path $out -Recurse -File
$sizeMb = [math]::Round((($files | Measure-Object Length -Sum).Sum) / 1MB, 1)
$zipMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "できました:"
Write-Host "  フォルダ: $out  ($($files.Count) ファイル、$sizeMb MB)"
Write-Host "  zip     : $zip  ($zipMb MB)"

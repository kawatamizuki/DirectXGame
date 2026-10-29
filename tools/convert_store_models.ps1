# コンビニ・スーパー・映画館のモデル(Sketchfabの GLB)を、ゲームが読める形(OBJ + MTL + アトラス1枚)に変換して、Assets/Models/Stores/ に出力する。
# 変換の中身は tools/convert_model.py(Blenderの--backgroundで動かす)。条件(向き・切り出し・地面の削除)は、モデルごとに違うので、ここに残す。
#
# 使い方(リポジトリのルートで):
#   powershell -ExecutionPolicy Bypass -File tools\convert_store_models.ps1 [-Source C:\Users\you\Downloads] [-Blender "C:\...\blender.exe"]
#
# 元のGLB(CC BY 4.0)は、リポジトリに入れない(*.glb は .gitignore 済み)。出力のOBJ/MTLも、これまで通り .gitignore の対象(*.obj, *.mtl)。
# 出力フォルダの CREDITS.txt(作者・出典・ライセンス・加工した旨)は、リポジトリに入れる。クレジットは THIRD_PARTY_LICENSES.md にも書く。
param(
    [string]$Source = (Join-Path $env:USERPROFILE "Downloads"),
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot "convert_model.py"
$outRoot = Join-Path $root "Assets\Models\Stores"

if (-not (Test-Path $Blender)) { throw "Blender が見つかりません: $Blender (-Blender で指定してください)" }

# 変換の条件。座標は、元のglTFの座標(Y上。正面は+Z)。
#  - crop: 残す範囲 X0 X1 Z0 Z1(スーパーは、道路・駐車場・車を含む敷地全体なので、店舗と前の歩道だけを切り出す)
#  - rotateY: 正面を+Zにそろえる回転(度)
#  - dropGround: 一番低い所からこの高さ以内の水平な面(地面・床)を消す(映画館は、2833x1973の巨大な地面が付いている)
#  - doubleSided: 全ての面を両面にする(映画館は、壁が内向きに作られていて、そのままだと、外から壁が見えない)
$models = @(
    @{ Name = "convenience"; File = "mini_market.glb"; RotateY = -90; Crop = $null; DropGround = $null
       Title = "Mini Market"; Author = "Elbolillo (https://sketchfab.com/Elbolilloduro)"
       Url = "https://sketchfab.com/3d-models/mini-market-725af75672704fc599a692d64e946475" },
    @{ Name = "supermarket"; File = "lowpoly_supermarket.glb"; RotateY = 0; Crop = @(-8.1, 8.1, -7.6, 1.5); DropGround = $null
       Title = "Lowpoly Supermarket"; Author = "Klieg3D (https://sketchfab.com/Klieg3D)"
       Url = "https://sketchfab.com/3d-models/lowpoly-supermarket-80f32e7d620147dda44445f79bba4281" },
    @{ Name = "cinema"; File = "cinema.glb"; RotateY = 0; Crop = $null; DropGround = 7.0; DoubleSided = $true
       Title = "Cinema"; Author = "TRYFIELD (https://sketchfab.com/tryfield)"
       Url = "https://sketchfab.com/3d-models/cinema-c74081d273324d409cb2350a2a349a80" },
    # カフェ: FBX(材質入り)から変換する。同じ内容のOBJは、材質(.mtl)が付いていなくて、色が失われるので、FBXを使う。
    # CGTraderの無料モデル(ID #2129997、2019-10-17公開)。ライセンスは Royalty Free License (no AI)(2026-10-29に確認)。
    # → ゲームへの組み込みは可。ただし、素材を、エンドユーザーが元の形で取り出せない形で入れる条件がある。
    #   このフォルダのOBJ・PNGは、そのまま配布物に入れてはいけない(make_release_package.ps1 は、代わりにKenneyの建物を入れる)。
    # 元に、600x600の巨大な地面が付いているので、削除する。正面は、元は-Z側(看板・入口)なので、180度回して+Zにそろえる。
    @{ Name = "cafe"; File = "coffee_store.fbx"; RotateY = 180; Crop = $null; DropGround = 0.3
       Title = "Coffee Store"; Author = "w-murderface (https://www.cgtrader.com/designers/w-murderface)"
       Url = "https://www.cgtrader.com/free-3d-models/exterior/cityscape/coffee-store"
       License = "CGTrader Royalty Free License (no AI). In-game use only: do NOT redistribute these files as-is or make them extractable by end users" }
)

foreach ($m in $models) {
    $in = Join-Path $Source $m.File
    if (-not (Test-Path $in)) { throw "元のファイルが見つかりません: $in" }
    $out = Join-Path $outRoot $m.Name
    $args = @("--background", "--python-exit-code", "1", "--python", $script, "--",
        "--in", $in, "--out-dir", $out, "--name", $m.Name, "--rotate-y", [string]$m.RotateY,
        "--title", $m.Title, "--author", $m.Author, "--source", $m.Url,
        "--license", $(if ($m.License) { $m.License } else { "CC BY 4.0 (http://creativecommons.org/licenses/by/4.0/)" }))
    if ($m.Crop) { $args += @("--crop") + ($m.Crop | ForEach-Object { [string]$_ }) }
    if ($null -ne $m.DropGround) { $args += @("--drop-ground", [string]$m.DropGround) }
    if ($m.DoubleSided) { $args += "--double-sided" }

    Write-Host "=== $($m.Name) ($($m.File))"
    # Blenderは、警告を標準エラーに出す。ErrorActionPreference=Stopのままだと、それだけで止まるので、ここだけ緩める。
    $ErrorActionPreference = "Continue"
    & $Blender @args 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^\[convert\]|Traceback|Error' } | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($exitCode -ne 0) { throw "変換に失敗しました: $($m.Name) (終了コード $exitCode)" }
}
Write-Host "完了: $outRoot"

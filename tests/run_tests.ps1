# 単体テストを、まとめてビルドして、実行する。
#   使い方:  powershell -File tests\run_tests.ps1            (全部)
#            powershell -File tests\run_tests.ps1 -Filter p1  (名前に p1 を含む物だけ)
# 結果の一覧を出し、1つでも失敗(ビルド失敗を含む)したら、終了コード1で終わる。
# 生成物(.obj/.exe)は、リポジトリの外(%TEMP%\GAME_tests)に出す。
param(
    [string]$Filter = ""
)

$ErrorActionPreference = "Stop"
$testsDir = $PSScriptRoot
$root = Split-Path $testsDir -Parent
$out = Join-Path $env:TEMP "GAME_tests"
New-Item -ItemType Directory -Force $out | Out-Null

# --- Visual Studio のビルド環境(cl.exe)を読み込む ---
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars))
{
    Write-Host "vcvars64.bat が見つからない: $vcvars" -ForegroundColor Red
    exit 2
}
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

# --- 全てのテストが共有する、ゲーム側のソース(画面・描画に依存しない物だけ) ---
$common = @(
    "Simulation\ResidentLogic.cpp", "Simulation\SimDebugRecorder.cpp", "Simulation\SimTimeline.cpp",
    "Simulation\SimTest.cpp", "Simulation\SimStats.cpp", "Simulation\SimRunDigest.cpp",
    "Simulation\SimInvariantMonitor.cpp", "Simulation\FacilityData.cpp", "Simulation\FacilityLedger.cpp",
    "Simulation\CommercialArea.cpp", "Simulation\CommercialAreaManager.cpp", "Simulation\AreaLedger.cpp", "Simulation\AreaIndicators.cpp", "Simulation\AreaReputation.cpp", "Simulation\RoadProximity.cpp",
    "Object\BuildingType.cpp", "Field\OrientedRectangleOverlap.cpp", "Core\GameClock.cpp", "Scenario\Json.cpp", "Scenario\PathGuard.cpp", "Scenario\AtomicFile.cpp", "Scenario\TestMapStore.cpp", "Scenario\TestMapSession.cpp"
) | ForEach-Object { Join-Path $root $_ }

$includes = @("Simulation", "Core", "Field", "Object", "Math", "Graphics", "Scenario", "DebugTools", "UI") |
    ForEach-Object { "/I`"$(Join-Path $root $_)`"" }
$includes += "/I`"$(Join-Path $testsDir 'stubs')`""     # imgui.h の代用品(本物の ImGui は使わない)
$flags = @("/nologo", "/std:c++17", "/EHsc", "/utf-8", "/DNOMINMAX", "/W3", "/MD") + $includes

# --- 共有ソースを、1回だけ、コンパイルする ---
$objDir = Join-Path $out "obj"
Remove-Item -Recurse -Force $objDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $objDir | Out-Null
Write-Host "共有ソースをコンパイル中 ($($common.Count) 本)..."
& cl @flags /c "/Fo$objDir\" @common 2>&1 | Where-Object { $_ -match 'error|warning C4(?!819)' } | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0)
{
    Write-Host "共有ソースのコンパイルに失敗" -ForegroundColor Red
    exit 1
}
$objs = Get-ChildItem $objDir -Filter *.obj | ForEach-Object { $_.FullName }

# --- 各テストを、ビルドして実行する ---
$tests = Get-ChildItem $testsDir -Filter *_test.cpp | Where-Object { $_.Name -like "*$Filter*" } | Sort-Object Name
$results = @()
foreach ($test in $tests)
{
    $name = $test.BaseName
    $exe = Join-Path $out "$name.exe"
    Remove-Item $exe -ErrorAction SilentlyContinue
    $log = & cl @flags $test.FullName @objs "/Fo$out\$name.obj" "/Fe$exe" 2>&1
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe))
    {
        $log | Where-Object { $_ -match 'error|LNK' } | Select-Object -First 8 | ForEach-Object { Write-Host $_ }
        $results += [pscustomobject]@{ Name = $name; Result = "BUILD FAIL"; Detail = "" }
        continue
    }
    $run = & $exe 2>&1
    $code = $LASTEXITCODE
    $last = ($run | Select-Object -Last 1)
    if ($code -ne 0)
    {
        $run | Where-Object { $_ -match 'FAIL' } | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
    }
    $results += [pscustomobject]@{ Name = $name; Result = $(if ($code -eq 0) { "PASS" } else { "FAIL" }); Detail = "$last" }
}

Write-Host ""
Write-Host "==== 結果 ===="
foreach ($r in $results)
{
    $color = if ($r.Result -eq "PASS") { "Green" } else { "Red" }
    Write-Host ("{0,-12} {1,-32} {2}" -f $r.Result, $r.Name, $r.Detail) -ForegroundColor $color
}
$failed = @($results | Where-Object { $_.Result -ne "PASS" }).Count
Write-Host ("{0} 本中 {1} 本が成功" -f $results.Count, ($results.Count - $failed))
if ($results.Count -eq 0) { exit 1 }
exit $(if ($failed -eq 0) { 0 } else { 1 })

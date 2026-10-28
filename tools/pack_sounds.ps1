# 音のパック(Sounds\sounds.pak)を作る。音の表(Sounds\sounds.json)に書かれた音のファイル(mp3・wav)を、1つのファイルにまとめる。
# 使い方: PowerShell で、プロジェクトのフォルダから
#     .\tools\pack_sounds.ps1                      (Sounds\sounds.pak を作る)
#     .\tools\pack_sounds.ps1 -Output dist\GAME\Sounds\sounds.pak
# なぜ: BGMなどの素材は、「音源のファイルを、そのままの形で、利用者が取り出して複製できる状態で配ってはいけない」条件がある
#       (THIRD_PARTY_LICENSES.md)。配布する実行環境には、mp3を入れずに、このパックを入れる。形式は Audio\SoundPack.h。
# 注意: かき混ぜ方(Scramble)は、Audio\SoundPack.cpp と完全に同じにすること(違うと、ゲームが読めない)。
param(
    [string]$Output = 'Sounds\sounds.pak'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;

public static class SoundPackWriter
{
    const uint Key = 0xA3C59AC3u;

    public static uint Hash(byte[] data, int offset, int count)
    {
        uint h = 2166136261u;
        for (int i = 0; i < count; ++i) { h ^= data[offset + i]; h = unchecked(h * 16777619u); }
        return h;
    }

    public static void Scramble(string name, byte[] data)
    {
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        uint state = Hash(nameBytes, 0, nameBytes.Length) ^ Key;
        if (state == 0) { state = 0x12345678u; }
        for (int i = 0; i < data.Length; ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            data[i] ^= (byte)(state >> 16);
        }
    }

    public static void Write(string path, string[] names, byte[][] datas)
    {
        string temp = path + ".tmp";
        using (var stream = new FileStream(temp, FileMode.Create, FileAccess.Write))
        using (var w = new BinaryWriter(stream))
        {
            w.Write(new byte[] { (byte)'G', (byte)'S', (byte)'P', (byte)'K' });
            w.Write((uint)1);
            w.Write((uint)names.Length);
            for (int i = 0; i < names.Length; ++i)
            {
                byte[] nameBytes = Encoding.UTF8.GetBytes(names[i]);
                w.Write((ushort)nameBytes.Length);
                w.Write(nameBytes);
                w.Write((uint)datas[i].Length);
                w.Write(Hash(datas[i], 0, datas[i].Length));
            }
            for (int i = 0; i < names.Length; ++i)
            {
                byte[] copy = (byte[])datas[i].Clone();
                Scramble(names[i], copy);
                w.Write(copy);
            }
        }
        if (File.Exists(path)) { File.Delete(path); }
        File.Move(temp, path);
    }
}
'@

$tablePath = Join-Path $root 'Sounds\sounds.json'
if (-not (Test-Path -LiteralPath $tablePath)) { throw "音の表がありません: $tablePath" }
$table = [IO.File]::ReadAllText($tablePath, [Text.Encoding]::UTF8) | ConvertFrom-Json

$names = New-Object System.Collections.Generic.List[string]
$datas = New-Object System.Collections.Generic.List[byte[]]
foreach ($sound in $table.sounds) {
    $file = [string]$sound.file
    if ($names.Contains($file)) { continue }       # 同じファイルを複数の音が使う時は、1回だけ入れる
    $source = Join-Path $root ($file -replace '/', '\')
    if (-not (Test-Path -LiteralPath $source)) { throw "音のファイルがありません: $file ($($sound.id))" }
    $names.Add($file)
    $datas.Add([IO.File]::ReadAllBytes($source))
}

$outPath = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $root $Output }
[IO.Directory]::CreateDirectory((Split-Path -Parent $outPath)) | Out-Null
[SoundPackWriter]::Write($outPath, $names.ToArray(), $datas.ToArray())

$size = [math]::Round((Get-Item -LiteralPath $outPath).Length / 1MB, 2)
Write-Host "音のパックを作りました: $outPath ($($names.Count) ファイル、$size MB)"

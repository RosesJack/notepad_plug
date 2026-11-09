$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$zig = Join-Path $PSScriptRoot 'tools\zig-windows-x86_64-0.13.0\zig.exe'
if (!(Test-Path $zig)) { throw '请先解压 Zig 0.13.0 Windows x64 到 tools 目录。' }
New-Item -ItemType Directory -Force dist | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot '.zig-cache'
& $zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -shared src/SearchShelf.cpp -o dist/SearchShelf.dll -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luxtheme
if ($LASTEXITCODE -ne 0) { throw '编译失败' }
Write-Host '已生成 dist\SearchShelf.dll（适用于 x64 Notepad++）'
& $zig c++ -target x86_64-windows-gnu -std=c++17 -O0 tests/config_test.cpp -o dist/config-test.exe -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luxtheme
if ($LASTEXITCODE -ne 0) { throw '测试编译失败' }
& .\dist\config-test.exe
if ($LASTEXITCODE -ne 0) { throw '配置测试失败' }
& $zig c++ -target x86_64-windows-gnu -std=c++17 -O0 tests/layout_test.cpp -o dist/layout-test.exe -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luxtheme
if ($LASTEXITCODE -ne 0) { throw '布局测试编译失败' }
& .\dist\layout-test.exe
if ($LASTEXITCODE -ne 0) { throw '布局测试失败' }

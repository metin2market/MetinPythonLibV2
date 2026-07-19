# Builds eXLib.dll (32-bit) from source and drops it in .\build\
# Requirements on this machine:
#   - 32-bit Python 2.7 at C:\Python27 (provides python27.lib + headers; game is 32-bit so this MUST be x86)
#   - VS 2022 with the C++ workload (VCTools) + a Windows 10/11 SDK; both are located at runtime,
#     so this runs unchanged on a dev box (Build Tools) and a CI runner (Enterprise)
#
# Notes on the flags (why they differ from the original v142 build):
#   /p:PlatformToolset=v143      -> we have the VS2022 toolset, not v142 (works fine; offsets are the game's, not ours)
#   /p:WholeProgramOptimization=false -> lets the v143 linker consume the old External\AAPathPlaning.lib
#                                        (built with v142) without a C1047 compiler-version mismatch
#   common/SimpleIni.h was patched to drop std::binary_function (removed from the modern MSVC STL)
#
# Output: .\build\eXLib.dll  — rename/copy to eXLib.mix to deploy to the bot.

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# Located via vswhere, not a fixed path: this box has VS Build Tools, CI runners have Enterprise.
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
  $msbuild = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuild -or -not (Test-Path $msbuild)) {
  throw "MSBuild with the C++ workload not found - install VS 2022 Build Tools (VCTools)."
}

# The SDK version is whatever is installed, not a pin - runners carry a different one than this box.
$sdk = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" -Directory -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -match '^10\.' } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw "No Windows 10/11 SDK found under Windows Kits\10\Include." }

Write-Host "MSBuild: $msbuild"
Write-Host "SDK:     $($sdk.Name)"

& $msbuild "$root\MetinPythonLib\MetinPythonLib.vcxproj" `
  /t:Rebuild `
  /p:Configuration=Release /p:Platform=Win32 `
  /p:SolutionDir="$root\" /p:OutDir="$root\build\" `
  /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=$($sdk.Name) `
  /p:WholeProgramOptimization=false `
  /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
  Write-Host "`nBuilt: $root\build\eXLib.dll" -ForegroundColor Green
} else {
  throw "Build failed (exit $LASTEXITCODE)"
}

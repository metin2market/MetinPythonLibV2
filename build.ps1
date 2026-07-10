# Builds eXLib.dll (32-bit) from source and drops it in .\build\
# Requirements on this machine:
#   - 32-bit Python 2.7 at C:\Python27 (provides python27.lib + headers; game is 32-bit so this MUST be x86)
#   - VS 2022 Build Tools with the C++ workload (VCTools) + a Windows 10/11 SDK
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
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild - is VS 2022 Build Tools installed?" }

& $msbuild "$root\MetinPythonLib\MetinPythonLib.vcxproj" `
  /t:Rebuild `
  /p:Configuration=Release /p:Platform=Win32 `
  /p:SolutionDir="$root\" /p:OutDir="$root\build\" `
  /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0 `
  /p:WholeProgramOptimization=false `
  /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
  Write-Host "`nBuilt: $root\build\eXLib.dll" -ForegroundColor Green
} else {
  throw "Build failed (exit $LASTEXITCODE)"
}

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

# Both the SDK and the v143 compiler minor version float with whatever the machine has, so they are
# recorded in BUILDINFO.txt below - a binary that crashes must be traceable back to what built it.
$vsPath = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
$vsVer = & $vswhere -latest -products * -property catalog_productDisplayVersion | Select-Object -First 1
$vcToolsFile = "$vsPath\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
$vcTools = if (Test-Path $vcToolsFile) { (Get-Content $vcToolsFile -Raw).Trim() } else { "unknown" }

Write-Host "MSBuild: $msbuild"
Write-Host "SDK:     $($sdk.Name)"
Write-Host "MSVC:    $vcTools (VS $vsVer)"

& $msbuild "$root\MetinPythonLib\MetinPythonLib.vcxproj" `
  /t:Rebuild `
  /p:Configuration=Release /p:Platform=Win32 `
  /p:SolutionDir="$root\" /p:OutDir="$root\build\" `
  /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=$($sdk.Name) `
  /p:WholeProgramOptimization=false `
  /v:minimal /nologo

if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

# Provenance record. The SHA256s are the point: hash a deployed eXLib.mix and you know exactly
# which build it came from, without guessing from timestamps.
$commit = try { (git -C $root rev-parse HEAD 2>$null).Trim() } catch { "unknown" }
$dirty = try { if ((git -C $root status --porcelain 2>$null)) { "dirty" } else { "clean" } } catch { "unknown" }
$describe = try { (git -C $root describe --tags --always 2>$null).Trim() } catch { "unknown" }

$lines = @(
  "eXLib build provenance",
  "======================",
  "built (UTC):      $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
  "git commit:       $commit ($dirty)",
  "git describe:     $describe",
  "",
  "toolchain",
  "  VS version:     $vsVer",
  "  MSVC toolset:   $vcTools",
  "  PlatformToolset: v143",
  "  Windows SDK:    $($sdk.Name)",
  "",
  "artifacts"
)
foreach ($f in @("eXLib.dll", "eXLib.pdb")) {
  $p = "$root\build\$f"
  if (Test-Path $p) {
    $h = (Get-FileHash $p -Algorithm SHA256).Hash
    $lines += "  {0,-10} sha256={1} size={2}" -f $f, $h, (Get-Item $p).Length
  }
}
$lines | Set-Content "$root\build\BUILDINFO.txt" -Encoding UTF8

Write-Host "`nBuilt: $root\build\eXLib.dll" -ForegroundColor Green
Get-Content "$root\build\BUILDINFO.txt" | Write-Host

[CmdletBinding()]
param(
	[ValidateSet("Release", "Debug")]
	[string]$Configuration = "Release",
	[switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$topDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$toolchain = Join-Path $topDir "zbin\glistzbin-win64\clang64\bin"
$cmake = Join-Path $topDir "zbin\glistzbin-win64\CMake\bin\cmake.exe"
$buildDir = Join-Path $env:TEMP "gipmultiplayer-team-voice-$($Configuration.ToLowerInvariant())"

if (-not (Test-Path -LiteralPath $cmake)) {
	throw "Glist CMake was not found at $cmake"
}

$env:PATH = "$toolchain;$env:PATH"

& $cmake -S $PSScriptRoot -B $buildDir -G "MinGW Makefiles" `
	"-DCMAKE_BUILD_TYPE=$Configuration" `
	"-DCMAKE_MAKE_PROGRAM=$(Join-Path $toolchain 'mingw32-make.exe')"
if ($LASTEXITCODE -ne 0) {
	throw "Team voice example configuration failed"
}

& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) {
	throw "Team voice example build failed"
}

$executable = Join-Path $buildDir "GlistApp.exe"
if (-not $BuildOnly) {
	Start-Process -FilePath $executable -WorkingDirectory $PSScriptRoot -Wait
}

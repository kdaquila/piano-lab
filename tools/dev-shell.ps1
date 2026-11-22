<#
.SYNOPSIS
    Sets up the Visual Studio C++ build environment (MSVC, Windows SDK) in the current PowerShell.

.DESCRIPTION
    CMake + Ninja with MSVC needs the compiler's environment variables, which a plain terminal
    doesn't have. Dot-source this script once per terminal:

        . .\tools\dev-shell.ps1

    It also makes sure CMake, Ninja and LLVM are on PATH, even in terminals opened before they
    were installed. Running it again in the same terminal does nothing.
#>

if ($env:VSCMD_VER -and (Get-Command cl -ErrorAction SilentlyContinue)) {
    return
}

# Pick up PATH changes from tool installs made after this terminal opened.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio was not found (vswhere.exe is missing). Install Visual Studio 2022 with the C++ workload.'
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'No Visual Studio installation with the C++ tools (MSVC x64) was found.'
}

# VS's own setup scripts call vswhere too, and expect it on PATH.
$env:Path = (Split-Path $vswhere) + ';' + $env:Path

Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

foreach ($tool in 'cl', 'cmake', 'ninja', 'clang-format', 'clang-tidy') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Warning "$tool is not on PATH. See the README's setup section."
    }
}

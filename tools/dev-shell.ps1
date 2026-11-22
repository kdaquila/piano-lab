<#
.SYNOPSIS
    Sets up the Visual Studio C++ build environment (MSVC x64, Windows SDK) in the current PowerShell.

.DESCRIPTION
    CMake + Ninja with MSVC needs the compiler's environment variables, which a plain terminal
    doesn't have. Dot-source this script once per terminal:

        . .\tools\dev-shell.ps1

    It also makes sure CMake, Ninja and LLVM are on PATH, even in terminals opened before they
    were installed, without removing anything already on this terminal's PATH. Running it again in
    a terminal that already has the x64 environment only refreshes PATH.

    Fails if a required tool is missing.
#>

# Add PATH entries from the registry (tool installs made after this terminal opened) that this
# session doesn't have yet. Existing entries, including session-only ones such as an activated
# virtual environment, are kept, in their order.
function Add-MissingPathEntries {
    $current = @($env:Path -split ';' | Where-Object { $_ })
    $known = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $current) { [void] $known.Add($entry.TrimEnd('\')) }

    $registry = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
    $missing = @($registry -split ';' | Where-Object { $_ -and $known.Add($_.TrimEnd('\')) })
    if ($missing) {
        $env:Path = ($current + $missing) -join ';'
    }
}

Add-MissingPathEntries

# Reuse the environment only if it's already the x64 MSVC one. A Developer PowerShell opened from
# the Start menu defaults to x86, which would silently produce a 32-bit build.
$hasX64Env = $env:VSCMD_VER -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and
             $env:VSCMD_ARG_HOST_ARCH -eq 'x64' -and (Get-Command cl -ErrorAction SilentlyContinue)

if (-not $hasX64Env) {
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
}

$missingTools = @('cl', 'cmake', 'ninja', 'clang-format', 'clang-tidy' |
    Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) })
if ($missingTools) {
    throw "Not on PATH: $($missingTools -join ', '). See the README's setup section."
}

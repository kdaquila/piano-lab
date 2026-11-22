<#
.SYNOPSIS
    The one quality gate: format, build, test, lint, and test again under AddressSanitizer.

.DESCRIPTION
    The same check for people, agents and (later) CI. Stops at the first failing stage.

        .\tools\check.ps1            # check everything, change nothing
        .\tools\check.ps1 -Fix       # reformat files with clang-format first, then check
        .\tools\check.ps1 -SkipAsan  # skip the AddressSanitizer build (faster)

.PARAMETER Fix
    Apply clang-format to the C++ files instead of only reporting badly formatted ones.

.PARAMETER SkipAsan
    Skip the AddressSanitizer build and tests.
#>
[CmdletBinding()]
param(
    [switch] $Fix,
    [switch] $SkipAsan
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot

function Invoke-Stage([string] $name, [scriptblock] $body) {
    Write-Host "`n=== $name" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    & $body
    if ($LASTEXITCODE -ne 0) {
        throw "$name failed (exit code $LASTEXITCODE)."
    }
    Write-Host ("--- {0}: OK ({1:n1} s)" -f $name, $watch.Elapsed.TotalSeconds) -ForegroundColor Green
}

try {
    . (Join-Path $PSScriptRoot 'dev-shell.ps1')

    # Our C++ sources: tracked or new, never ignored files or dependencies.
    $cppFiles = @(git ls-files --cached --others --exclude-standard -- engine backend |
        Where-Object { $_ -match '\.(cpp|hpp|h)$' })
    $sourceFiles = @($cppFiles | Where-Object { $_ -match '\.cpp$' })

    Invoke-Stage 'Format (clang-format)' {
        if ($Fix) {
            clang-format -i $cppFiles
        } else {
            clang-format --dry-run --Werror $cppFiles
        }
    }

    Invoke-Stage 'Configure (dev)' { cmake --preset dev }
    Invoke-Stage 'Build (dev)' { cmake --build --preset dev }
    Invoke-Stage 'Test (dev)' { ctest --preset dev }

    Invoke-Stage 'Lint (clang-tidy)' {
        # One clang-tidy per file, in parallel. It reads the exact build flags from the dev
        # preset's compile_commands.json.
        $failed = $sourceFiles | ForEach-Object -ThrottleLimit ([Environment]::ProcessorCount) -Parallel {
            $output = clang-tidy -p build/dev --quiet $_ 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host ($output -join "`n")
                $_
            }
        }
        if ($failed) {
            Write-Host "clang-tidy found problems in: $($failed -join ', ')" -ForegroundColor Red
            $global:LASTEXITCODE = 1
        } else {
            $global:LASTEXITCODE = 0
        }
    }

    if (-not $SkipAsan) {
        Invoke-Stage 'Configure (asan)' { cmake --preset asan }
        Invoke-Stage 'Build (asan)' { cmake --build --preset asan }
        Invoke-Stage 'Test (asan)' { ctest --preset asan }
    }

    Write-Host "`nAll checks passed." -ForegroundColor Green
} catch {
    Write-Host "`nCHECK FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}

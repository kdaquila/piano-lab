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

# clang-format and clang-tidy output can change between LLVM major versions, so everyone (people,
# agents, CI) uses the same one. Bump together with the README when upgrading.
$requiredLlvmMajor = 23

function Invoke-Stage([string] $name, [scriptblock] $body) {
    Write-Host "`n=== $name" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    & $body
    if ($LASTEXITCODE -ne 0) {
        throw "$name failed (exit code $LASTEXITCODE)."
    }
    Write-Host ("--- {0}: OK ({1:n1} s)" -f $name, $watch.Elapsed.TotalSeconds) -ForegroundColor Green
}

# The check sets up the Visual Studio environment in this process. Snapshot the caller's
# environment so the terminal is left exactly as it was afterwards.
$savedEnv = @{}
Get-ChildItem env: | ForEach-Object { $savedEnv[$_.Name] = $_.Value }

$exitCode = 0
Push-Location $repoRoot
try {
    . (Join-Path $PSScriptRoot 'dev-shell.ps1')

    $llvmVersion = (clang-format --version) -join ' '
    if ($llvmVersion -notmatch 'version (\d+)\.' -or [int] $Matches[1] -ne $requiredLlvmMajor) {
        throw "LLVM $requiredLlvmMajor is required for clang-format and clang-tidy, found: $llvmVersion"
    }

    # Our C++ sources: tracked or new, never ignored files or dependencies.
    $gitFiles = @(git ls-files --cached --others --exclude-standard -- engine backend)
    if ($LASTEXITCODE -ne 0) {
        throw 'git ls-files failed; is this a git checkout with git on PATH?'
    }
    $cppFiles = @($gitFiles | Where-Object { $_ -match '\.(cpp|hpp|h)$' })
    $sourceFiles = @($cppFiles | Where-Object { $_ -match '\.cpp$' })
    if (-not $sourceFiles) {
        throw 'No C++ source files found under engine/ or backend/.'
    }

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
        # clang-tidy needs each file's exact build flags from the dev preset's compile database.
        # A .cpp that isn't in it isn't part of any target, so it is never built: report that
        # instead of letting clang-tidy guess the flags.
        $database = Get-Content build/dev/compile_commands.json -Raw | ConvertFrom-Json
        $built = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $database) {
            [void] $built.Add([IO.Path]::GetFullPath($entry.file, $entry.directory))
        }
        $notBuilt = @($sourceFiles | Where-Object { -not $built.Contains([IO.Path]::GetFullPath($_)) })
        if ($notBuilt) {
            Write-Host "Not part of any CMake target (add them to a CMakeLists.txt): $($notBuilt -join ', ')" -ForegroundColor Red
            $global:LASTEXITCODE = 1
            return
        }

        # One clang-tidy per file, in parallel.
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
    $exitCode = 1
} finally {
    Pop-Location
    # Restore the caller's environment: drop variables the check added, reset the rest.
    Get-ChildItem env: | Where-Object { -not $savedEnv.ContainsKey($_.Name) } |
        ForEach-Object { Remove-Item "env:$($_.Name)" }
    foreach ($name in $savedEnv.Keys) {
        Set-Item "env:$name" $savedEnv[$name]
    }
}
exit $exitCode

---
paths:
  - "tools/**"
  - "**/*.ps1"
---

# PowerShell scripts

- Scripts target **Windows PowerShell 5.1** (`powershell.exe`), the version built into Windows.
- Don't use PowerShell 7-only features, for example `ForEach-Object -Parallel`, the ternary and `??` operators, `&&` / `||` pipeline chains, or two-argument `[IO.Path]::GetFullPath`.
- 5.1 doesn't move .NET's working directory with `Push-Location`: give .NET calls absolute paths.
- Test scripts with `powershell.exe`, never only with `pwsh`.

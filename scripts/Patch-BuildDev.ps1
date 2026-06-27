# Patch Ninja rules.ninja after CMake configure on Windows.
# Fixes:
#   1. Unquoted cmcldeps.exe path when CMake is installed under "Program Files"
#   2. Korean MSVC dependency prefix breaking cmcldeps (expects English)
#   3. UTF-8 BOM that can confuse Ninja on some setups
#
# Usage:
#   .\scripts\Patch-BuildDev.ps1                 # patches build/ and build-dev/
#   .\scripts\Patch-BuildDev.ps1 -BuildDir build

param(
    [string]$BuildDir = ""
)

function Patch-RulesNinja {
    param([string]$RulesPath)

    if (-not (Test-Path $RulesPath)) {
        Write-Host "Skip (not found): $RulesPath"
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($RulesPath)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $bytes = $bytes[3..($bytes.Length - 1)]
        Write-Host "Removed UTF-8 BOM from $RulesPath"
    }

    $content = [System.Text.Encoding]::UTF8.GetString($bytes)
    $patched = $content

    $patched = $patched -replace '(?<!["])D:/Program Files/CMake/bin/cmcldeps\.exe(?!["])', '"D:/Program Files/CMake/bin/cmcldeps.exe"'
    $patched = $patched -replace '(?<!["])C:/Program Files/CMake/bin/cmcldeps\.exe(?!["])', '"C:/Program Files/CMake/bin/cmcldeps.exe"'
    $patched = $patched -replace '"참고: 포함 파일: "', '"Note: including file: "'
    $patched = $patched -replace '"참고:[^"]*"', '"Note: including file: "'

    if ($patched -ne $content) {
        [System.IO.File]::WriteAllBytes($RulesPath, [System.Text.Encoding]::UTF8.GetBytes($patched))
        Write-Host "Patched $RulesPath"
    } else {
        Write-Host "No patch needed for $RulesPath"
    }
}

$root = Split-Path -Parent $PSScriptRoot
if ($BuildDir -ne "") {
    if ([System.IO.Path]::IsPathRooted($BuildDir)) {
        Patch-RulesNinja (Join-Path $BuildDir "CMakeFiles\rules.ninja")
    } else {
        Patch-RulesNinja (Join-Path $root "$BuildDir\CMakeFiles\rules.ninja")
    }
} else {
    foreach ($dir in @("build", "build-dev")) {
        Patch-RulesNinja (Join-Path $root "$dir\CMakeFiles\rules.ninja")
    }
}

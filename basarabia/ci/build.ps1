# ============================================================================
#  Basarabia RP - build complet al clientului (rulează pe Windows, în GitHub
#  Actions sau pe orice PC/VPS Windows cu Visual Studio 2022).
#
#  Folosește modulele oficiale Cfx din code\tools\ci\psm1, apoi asamblează
#  clientul în dist\ (BasarabiaRP.exe + "Basarabia RP.app\") pentru installer.
# ============================================================================

using module ..\..\code\tools\ci\psm1\cfxBuildContext.psm1
using module ..\..\code\tools\ci\psm1\cfxBuildTools.psm1
using module ..\..\code\tools\ci\psm1\cfxBuildUI.psm1
using module ..\..\code\tools\ci\psm1\cfxRunMSBuild.psm1
using module ..\..\code\tools\ci\psm1\cfxRunPrebuild.psm1
using module ..\..\code\tools\ci\psm1\cfxRunPremake.psm1
using module ..\..\code\tools\ci\psm1\cfxSetupCEF.psm1
using module ..\..\code\tools\ci\psm1\cfxSetupPrivate.psm1
using module ..\..\code\tools\ci\psm1\cfxSetupSubmodules.psm1
using module ..\..\code\tools\ci\psm1\cfxSetupVS.psm1
using module ..\..\code\tools\ci\psm1\cfxUpdateVersionFiles.psm1
using module ..\..\code\tools\ci\psm1\cfxVersions.psm1

$ErrorActionPreference = "Stop"

function Step([string] $name, [scriptblock] $body) {
    $started = Get-Date
    Write-Host "`n==================== $name ====================" -ForegroundColor Cyan
    & $body
    Write-Host ("---- {0} gata în {1:hh\:mm\:ss}" -f $name, ((Get-Date) - $started)) -ForegroundColor Green
}

function Copy-IfExists([string] $from, [string] $to) {
    if (Test-Path $from) {
        Copy-Item -Force -Recurse $from $to
    } else {
        Write-Host "  (lipsește, sărit: $from)" -ForegroundColor Yellow
    }
}

try {
    $ctx = Get-CfxBuildContext -RequireProductName
    $tools = Get-CfxBuildTools -Context $ctx
    $versions = Get-CfxVersions -Context $ctx

    $tools.ensurePython()
    $tools.ensureNodeJS()
    $tools.ensureYarn()

    $ctx.startBuild()

    Step "Visual Studio" { Invoke-CfxSetupVS -Context $ctx -Tools $tools }
    Step "Submodule"     { Invoke-SetupSubmodules -Root $ctx.ProjectRoot }
    Step "CEF"           { Invoke-CfxSetupCEF -Context $ctx -Tools $tools }
    Step "Prebuild"      { Invoke-RunPrebuild -Context $ctx }
    # Componentele private/închise Cfx - doar dacă sunt setate FIVEM_PRIVATE_URI
    # și FIVEM_CLOSED_URI (acces oferit de Cfx.re). Altfel se sare automat.
    Step "Private Cfx"   { Invoke-CfxSetupPrivate -Context $ctx -Versions $versions }
    Step "Premake"       { Invoke-RunPremake -Context $ctx -Tools $tools }
    Step "Versiuni"      { Invoke-UpdateVersionFiles -Context $ctx -Versions $versions }
    Step "Compilare C++" { Invoke-RunMSBuild -Context $ctx -Tools $tools }
    Step "UI"            { Invoke-BuildUI -Context $ctx -Versions $versions }

    Step "Asamblare client Basarabia RP" {
        $root    = $ctx.ProjectRoot
        $binRoot = $ctx.MSBuildOutput
        $dist    = Join-Path $root "dist"
        $app     = Join-Path $dist "Basarabia RP.app"

        Remove-Item -Force -Recurse -ErrorAction Ignore $dist
        New-Item -ItemType Directory -Force "$app\bin\cef", "$app\citizen" | Out-Null

        # CEF (browserul din joc: meniu, NUI, telefoane etc.)
        Copy-Item -Force "$root\vendor\cef\Release\*.dll"               "$app\bin\"
        Copy-Item -Force "$root\vendor\cef\Release\*.bin"               "$app\bin\"
        Copy-Item -Force "$root\vendor\cef\Resources\icudtl.dat"        "$app\bin\"
        Copy-Item -Force "$root\vendor\cef\Resources\*.pak"             "$app\bin\cef\"
        Copy-Item -Force "$root\vendor\cef\Resources\locales\en-US.pak" "$app\bin\cef\"

        # Date statice
        Copy-Item -Force -Recurse "$root\data\shared\*"     "$app\"
        Copy-Item -Force -Recurse "$root\data\client\*"     "$app\"
        Copy-Item -Force -Recurse "$root\data\redist\crt\*" "$app\bin\"

        # Rezultatul compilării
        Copy-Item -Force "$binRoot\*.dll"                      "$app\"
        Copy-Item -Force "$binRoot\*.com"                      "$app\"
        Copy-Item -Force "$binRoot\CitizenFX_SubProcess_*.bin" "$app\"
        Copy-Item -Force -Recurse "$binRoot\citizen\*"         "$app\citizen\"
        Copy-IfExists "$binRoot\FiveM_Diag.exe"                "$app\"
        Copy-IfExists "$binRoot\RGL-ElevationHelper.exe"       "$app\"

        # UI
        Copy-Item -Force "$root\ext\ui-build\data.zip"     "$app\citizen\ui.zip"
        Copy-Item -Force "$root\ext\ui-build\data_big.zip" "$app\citizen\ui-big.zip"

        # Versiuni
        $versions.Game    | Out-File -Encoding ascii "$app\citizen\version.txt"
        "$($versions.BuildID)" | Out-File -Encoding ascii "$app\citizen\release.txt"

        # Executabilul principal
        $exe = Join-Path $binRoot "BasarabiaRP.exe"
        if (!(Test-Path $exe)) { throw "Nu găsesc $exe - verificați compilarea." }
        Copy-Item -Force $exe "$dist\BasarabiaRP.exe"
        Copy-Item -Force $exe "$app\CitizenFX.exe"

        Write-Host ("Client asamblat în {0} ({1:N0} MB)" -f $dist,
            ((Get-ChildItem -Recurse $dist | Measure-Object Length -Sum).Sum / 1MB))
    }

    $ctx.finishBuild()
}
catch {
    Write-Host $_ -ForegroundColor Red
    Write-Host "BUILD EȘUAT" -ForegroundColor Red
    exit 1
}

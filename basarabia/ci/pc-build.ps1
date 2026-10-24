# ============================================================================
#  Basarabia RP - compilare launcher pe PC-ul propriu (fara GitHub Actions)
#
#  Cum se foloseste (o singura data se instaleaza tot, apoi doar se reporneste):
#    1. Click dreapta pe Start -> "Terminal (Admin)" / "Windows PowerShell (Admin)"
#    2. Lipiti:
#         Set-ExecutionPolicy -Scope Process Bypass -Force; irm https://raw.githubusercontent.com/pesteraumihai71-hue/fivem/master/basarabia/ci/pc-build.ps1 -OutFile $env:TEMP\brp-build.ps1; & $env:TEMP\brp-build.ps1 -Version 1.0.7
#    3. Lasati PC-ul pornit. La final installerul apare pe Desktop.
#
#  Ce face: instaleaza Visual Studio 2022 Community (C++), Git, Python, Node.js,
#  MSYS2, Inno Setup; descarca codul in C:\brp; compileaza; face installerul.
# ============================================================================
param(
    [switch] $Clean,
    [string] $Version = "1.0.7",
    [string] $Branch  = "master",
    [string] $Root    = "C:\brp"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Say([string] $t, [string] $c = "Cyan") { Write-Host "`n>>> $t" -ForegroundColor $c }
function Fail([string] $t) { Write-Host "`nEROARE: $t" -ForegroundColor Red; Read-Host "Apasati Enter pentru a inchide"; exit 1 }

# --- admin ---------------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Fail "Porniti PowerShell ca Administrator (click dreapta pe Start -> Terminal (Admin))." }

# --- verificari ----------------------------------------------------------
Say "Verific calculatorul"
$drive = Get-PSDrive -Name ($Root.Substring(0,1))
$freeGB = [math]::Round($drive.Free / 1GB)
$ramGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
Write-Host "  Spatiu liber pe $($drive.Name): $freeGB GB   |   RAM: $ramGB GB"
if (-not (Test-Path "$Root\.git") -and $freeGB -lt 110) { Fail "Trebuie cel putin ~120 GB liberi pe $($drive.Name): (acum: $freeGB GB)." }
if ($ramGB -lt 12) { Write-Host "  Atentie: sub 16 GB RAM compilarea va fi lenta." -ForegroundColor Yellow }
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "Lipseste winget. Instalati 'App Installer' din Microsoft Store si reporniti scriptul." }

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

function Ensure-Winget([string] $id, [scriptblock] $test, [string[]] $extra = @()) {
    if ($test -and (& $test)) { Write-Host "  ok: $id"; return }
    Write-Host "  instalez: $id ..." -ForegroundColor Yellow
    $wargs = @("install", "--id", $id, "-e", "--accept-package-agreements", "--accept-source-agreements", "--silent") + $extra
    & winget @wargs
    Refresh-Path
}

# --- programe necesare ---------------------------------------------------
Say "Instalez programele necesare (prima data dureaza 30-60 min)"
Ensure-Winget "Microsoft.PowerShell"  { Get-Command pwsh -ErrorAction SilentlyContinue }
Ensure-Winget "Git.Git"               { Get-Command git -ErrorAction SilentlyContinue }
Ensure-Winget "Python.Python.3.11"    { Get-Command python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' } }
Ensure-Winget "OpenJS.NodeJS.LTS"     { Get-Command node -ErrorAction SilentlyContinue }
Ensure-Winget "MSYS2.MSYS2"           { Test-Path C:\msys64\usr\bin\pacman.exe }
Ensure-Winget "JRSoftware.InnoSetup"  { (Test-Path "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") -or (Test-Path "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsComponents = @(
    "Microsoft.VisualStudio.Workload.NativeDesktop",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "Microsoft.VisualStudio.Component.VC.ATL",
    "Microsoft.VisualStudio.Component.Windows11SDK.22000",
    "Microsoft.Net.Component.4.6.TargetingPack",
    "Microsoft.Net.Component.4.8.SDK"
)
$hasVs = (Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if (-not $hasVs) {
    $override = "--wait --passive --norestart --includeRecommended " + (($vsComponents | ForEach-Object { "--add $_" }) -join " ")
    Ensure-Winget "Microsoft.VisualStudio.2022.Community" $null @("--override", $override)
} else {
    # completam componentele lipsa (ATL, SDK 22000)
    $vs = & $vswhere -latest -products * -property installationPath
    $add = @(); foreach ($c in $vsComponents) { $add += @("--add", $c) }
    Write-Host "  ok: Visual Studio ($vs) - verific componentele..."
    Start-Process "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe" -Wait -ArgumentList (@("modify", "--installPath", "`"$vs`"", "--quiet", "--norestart", "--includeRecommended") + $add) | Out-Null
}
if (-not ((Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath))) {
    Fail "Visual Studio 2022 (C++) nu s-a instalat. Reporniti calculatorul si rulati scriptul din nou."
}

Refresh-Path
python -m pip install --upgrade setuptools --quiet
if (-not (Get-Command yarn -ErrorAction SilentlyContinue)) { npm install -g yarn | Out-Null; Refresh-Path }

# --- codul ---------------------------------------------------------------
Say "Descarc / actualizez codul in $Root"
git config --global core.autocrlf false
git config --global core.symlinks true
git config --global core.longpaths true
if (Test-Path "$Root\.git") {
    git -C $Root fetch origin $Branch
    git -C $Root reset --hard "origin/$Branch"
    git -C $Root clean -fd
} else {
    git clone --branch $Branch https://github.com/pesteraumihai71-hue/fivem.git $Root
}
if ($LASTEXITCODE -ne 0) { Fail "Nu am putut descarca codul de pe GitHub." }
$sha = (git -C $Root rev-parse HEAD).Trim()

# --- Windows SDK 10.0.22000 (sau cel mai nou instalat) -------------------
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$want = "10.0.22000.0"
if (-not ((Test-Path "$kits\Include\$want\um") -and (Test-Path "$kits\Lib\$want\um\x64"))) {
    $latest = Get-ChildItem "$kits\Include" -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.0$' -and (Test-Path "$kits\Lib\$($_.Name)\um\x64") } |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1
    if (-not $latest) { Fail "Niciun Windows SDK instalat." }
    Write-Host "  SDK $want lipseste, folosesc $($latest.Name)" -ForegroundColor Yellow
    $pm = "$Root\code\premake5.lua"
    (Get-Content -Raw $pm).Replace("systemversion '$want'", "systemversion '$($latest.Name)'") | Set-Content -NoNewline $pm
}

# --- curatenie (dupa o cadere de curent fisierele intermediare pot fi stricate) ---
if ($Clean -and (Test-Path "$Root\.build-cache\build")) {
    Say "Sterg fisierele intermediare vechi (-Clean)"
    Get-Process mspdbsrv, MSBuild, cl, link -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$Root\.build-cache\build" -ErrorAction SilentlyContinue
}

# --- compilare -----------------------------------------------------------
Say "Compilez launcherul $Version (prima data 2-4 ore - lasati PC-ul pornit)"
$env:CI = "true"
$env:CI_COMMIT_REF_NAME = $Branch
$env:CI_COMMIT_SHA = $sha
$env:CI_PIPELINE_ID = ($Version -replace '\D', '')
$env:CFX_PRODUCT_NAME = "fivem"
$env:CI_PROJECT_DIR = $Root
# cu 16 GB RAM sau mai putin compilam mai putine fisiere deodata (altfel procesul poate fi oprit de Windows)
if ($ramGB -le 16) { $env:BRP_MSBUILD_CPUS = "4"; Write-Host "  RAM $ramGB GB: compilez cu 4 procese in paralel" }

# PC-ul nu are voie sa intre in Sleep cat compileaza
try { powercfg /change standby-timeout-ac 0; powercfg /change hibernate-timeout-ac 0 } catch {}

Push-Location $Root
# mesajele normale scrise de git/msbuild pe stderr nu trebuie sa opreasca scriptul
$ErrorActionPreference = "Continue"
try {
    & pwsh -NoProfile -ExecutionPolicy Bypass -File "$Root\basarabia\ci\build.ps1" *>&1 | Tee-Object -FilePath "$Root\build.log"
    $failed = ($LASTEXITCODE -ne 0) -or (Select-String -Quiet -Path "$Root\build.log" -Pattern "BUILD E.{1,3}UAT")
    if ($failed) { Fail "Compilarea a esuat. Trimiteti-mi fisierul $Root\build.log" }

    Say "Fac installerul"
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { Fail "Lipseste Inno Setup." }
    & $iscc "/DAppVersion=$Version" "$Root\basarabia\installer.iss"
    if ($LASTEXITCODE -ne 0) { Fail "Installerul nu s-a putut face." }
} finally {
    Pop-Location
}

$dest = Join-Path ([Environment]::GetFolderPath("Desktop")) "BasarabiaRP-Setup-$Version.exe"
Copy-Item -Force "$Root\basarabia\output\BasarabiaRP-Setup.exe" $dest
Say "GATA! Installerul e pe Desktop: $dest" "Green"
Read-Host "Apasati Enter pentru a inchide"

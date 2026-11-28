# Downloads a pinned LibreHardwareMonitor release (.NET Framework build, loadable from
# Windows PowerShell 5.1) and copies its library DLLs into ./lhm so PyInstaller can
# bundle them. RavenSpot reads GPU load and CPU/GPU temperatures through it.
# Used by build.bat and .github/workflows/build-windows.yml.
param(
    [string]$Dest = (Join-Path (Split-Path -Parent $PSScriptRoot) 'lhm')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Version = '0.9.6'
$Url = "https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v$Version/LibreHardwareMonitor.zip"
$Sha256 = '086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001'

if (Test-Path (Join-Path $Dest 'LibreHardwareMonitorLib.dll')) {
    Write-Host "LibreHardwareMonitor already present in $Dest"
    exit 0
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$work = Join-Path ([IO.Path]::GetTempPath()) ("ravenspot-lhm-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    $zip = Join-Path $work 'lhm.zip'
    Write-Host "Downloading LibreHardwareMonitor v$Version ..."
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    $hash = (Get-FileHash -Algorithm SHA256 -Path $zip).Hash.ToLower()
    if ($hash -ne $Sha256) { throw "SHA256 mismatch for LibreHardwareMonitor.zip: $hash" }

    $ex = Join-Path $work 'x'
    Expand-Archive -Path $zip -DestinationPath $ex -Force
    $lib = Get-ChildItem -Path $ex -Recurse -Filter 'LibreHardwareMonitorLib.dll' | Select-Object -First 1
    if (-not $lib) { throw 'LibreHardwareMonitorLib.dll not found in archive' }

    # Library + its dependencies only; skip the GUI app and its UI-only assemblies.
    $skip = @('Aga.Controls.dll', 'OxyPlot.dll', 'OxyPlot.WindowsForms.dll', 'Microsoft.Win32.TaskScheduler.dll')
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    Get-ChildItem -Path $lib.DirectoryName -Filter '*.dll' |
        Where-Object { $skip -notcontains $_.Name } |
        Copy-Item -Destination $Dest -Force
    $lic = Get-ChildItem -Path $ex -Recurse -Include 'LICENSE*' | Select-Object -First 1
    if ($lic) { Copy-Item $lic.FullName (Join-Path $Dest 'LICENSE-LibreHardwareMonitor.txt') -Force }
    Write-Host ("LibreHardwareMonitor v{0} -> {1} ({2} DLLs)" -f $Version, $Dest, (Get-ChildItem $Dest -Filter *.dll).Count)
}
finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

param([Parameter(Mandatory = $true)][string]$Installer)

$ErrorActionPreference = "Stop"
if ($env:GITHUB_ACTIONS -ne "true" -or $env:RUNNER_OS -ne "Windows") {
    throw "Run installer lifecycle tests only on a disposable Windows Actions runner"
}
$root = Join-Path $env:RUNNER_TEMP "RFdude installer test"
$logs = Join-Path $PWD "installer-logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
foreach ($name in @("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORMTHEME", "QT_QPA_PLATFORM", "QTDIR", "QT_ROOT_DIR")) {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:QT_QPA_PLATFORM = "windows"
$env:QT_DEBUG_PLUGINS = "1"
# This job never installs a Qt SDK; also exclude any runner Qt/Python plugin paths.
$env:PATH = (($env:PATH -split ";" | Where-Object { $_ -notmatch '(?i)(Qt|Python)' }) -join ";")

function Get-SystemQtState {
    $state = @{}
    foreach ($directory in @("$env:WINDIR\System32", "$env:WINDIR\SysWOW64")) {
        Get-ChildItem "$directory\Qt*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            $state[$_.FullName] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
        }
    }
    foreach ($scope in @("Machine", "User")) {
        $state["$scope PATH"] = [Environment]::GetEnvironmentVariable("PATH", $scope)
    }
    return $state
}

function Assert-SystemQtUnchanged($Before) {
    $after = Get-SystemQtState
    $keys = @($Before.Keys) + @($after.Keys)
    foreach ($key in ($keys | Select-Object -Unique)) {
        if ($Before[$key] -ne $after[$key]) { throw "Installer changed shared runtime or PATH: $key" }
    }
}

function Invoke-Installer([string]$Program, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "$Program failed with exit $($process.ExitCode)" }
}

function Test-Application {
    $executable = Join-Path $root "bin\RFdudeSmithChartV2.exe"
    foreach ($file in @($executable, (Join-Path $root "plugins\platforms\qwindows.dll"))) {
        if (-not (Test-Path $file)) { throw "Missing installed runtime: $file" }
    }
    $example = Join-Path $root "share\RFdudeSmithChartV2\examples\network.rfs_45Mfilter.json"
    if (-not (Test-Path $example)) { throw "Installed example is missing" }
    $process = Start-Process -FilePath $executable -ArgumentList "`"$example`"" -PassThru
    try {
        $deadline = (Get-Date).AddSeconds(30)
        do {
            $process.Refresh()
            if ($process.HasExited) { throw "Installed application exited early: $($process.ExitCode)" }
            if ($process.MainWindowHandle -ne 0) { break }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)
        if ($process.MainWindowHandle -eq 0) { throw "Installed application never created a native window" }
        $modules = @($process.Modules)
        $modules | ForEach-Object { $_.FileName } | Set-Content (Join-Path $logs "loaded-modules.txt")
        $qt = @($modules | Where-Object { $_.ModuleName -match '^Qt6.*\.dll$|^qwindows\.dll$' })
        if (-not ($qt | Where-Object { $_.ModuleName -eq "Qt6Widgets.dll" }) -or
            -not ($qt | Where-Object { $_.ModuleName -eq "qwindows.dll" })) {
            throw "Qt Widgets and the Windows platform plugin were not loaded"
        }
        foreach ($module in $qt) {
            if (-not $module.FileName.StartsWith("$root\", [StringComparison]::OrdinalIgnoreCase)) {
                throw "Qt module loaded outside installation: $($module.FileName)"
            }
        }
        if (-not $process.CloseMainWindow()) { throw "Could not close the installed application" }
        if (-not $process.WaitForExit(15000)) { throw "Application did not exit after closing its window" }
        if ($process.ExitCode -ne 0) { throw "Application exited with $($process.ExitCode)" }
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

$installerPath = (Resolve-Path $Installer).Path
$before = Get-SystemQtState
Invoke-Installer $installerPath @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/DIR=`"$root`"", "/LOG=`"$logs\install.txt`"")
Assert-SystemQtUnchanged $before
Test-Application
$shortcut = Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "RFdude Smith Chart V2.lnk"
if (-not (Test-Path $shortcut)) { throw "Start Menu shortcut is missing" }
$shell = New-Object -ComObject WScript.Shell
if ($shell.CreateShortcut($shortcut).TargetPath -ne (Join-Path $root "bin\RFdudeSmithChartV2.exe")) {
    throw "Start Menu shortcut does not target the installed executable"
}
Invoke-Installer $installerPath @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/DIR=`"$root`"", "/LOG=`"$logs\reinstall.txt`"")
Assert-SystemQtUnchanged $before
Test-Application
Invoke-Installer (Join-Path $root "unins000.exe") @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=`"$logs\uninstall.txt`"")
Assert-SystemQtUnchanged $before
if ((Test-Path $root) -or (Test-Path $shortcut)) {
    throw "Uninstall left the installation directory or Start Menu shortcut behind"
}
Write-Output "PASS: silent install, native window, private Qt modules, reinstall, uninstall"

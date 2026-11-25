param(
    [string]$EnginePath = $env:SATISFACTORY_ENGINE_PATH,
    [string]$GamePath = $env:SATISFACTORY_GAME_PATH,
    [ValidateSet('Windows', 'WindowsServer')][string]$Target = 'Windows',
    [string]$SharedToolsPath = $env:SATISFACTORY_SHARED_TOOLS
)
$ErrorActionPreference = 'Stop'
if ($Target -eq 'WindowsServer' -and (!$PSBoundParameters.ContainsKey('GamePath') -or !$GamePath)) {
    throw 'WindowsServer installation requires an explicit -GamePath for the server'
}
if (!$SharedToolsPath) {
    $SharedToolsPath = Join-Path $PSScriptRoot '..\..\..\..\.mod-development\Tools'
}
$builder = Join-Path $SharedToolsPath 'Build-Mod.ps1'
if (!(Test-Path -LiteralPath $builder -PathType Leaf)) { throw 'Shared build tools are missing; provide -SharedToolsPath' }
& $builder -ModPath (Join-Path $PSScriptRoot '..') -PackageVerifier (Join-Path $PSScriptRoot 'VerifyRelease.py') -EnginePath $EnginePath -GamePath $GamePath -Target $Target -PakRulesPath (Join-Path $PSScriptRoot 'PackageRules.ini')

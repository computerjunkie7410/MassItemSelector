param(
    [string]$EnginePath = $env:SATISFACTORY_ENGINE_PATH,
    [string]$SharedToolsPath = $env:SATISFACTORY_SHARED_TOOLS,
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'
if (!$SharedToolsPath) {
    $projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
    $SharedToolsPath = Join-Path $projectRoot '.mod-development\Tools'
}
$runner = Join-Path $SharedToolsPath 'Invoke-UnrealVerification.ps1'
if (!(Test-Path -LiteralPath $runner -PathType Leaf)) { throw 'Shared verification tools are missing; provide -SharedToolsPath' }
& $runner -ModPath (Join-Path $PSScriptRoot '..') -EnginePath $EnginePath -TimeoutSeconds $TimeoutSeconds `
    -Suite 'MassItemSelector.Selection' -MinimumTests 17

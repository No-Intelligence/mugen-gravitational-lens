param(
    [Parameter(Mandatory=$true)] [string]$GameDirectory,
    [string]$BaseCharacter='kfm',
    [ValidateSet('diagnostic','layer','max')] [string]$Case='layer',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')] [string]$OutputName='lens-live-repro'
)
$ErrorActionPreference = 'Stop'
$lensRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$lensSource = (Resolve-Path -LiteralPath $GameDirectory).Path
if ($BaseCharacter -notmatch '^[a-zA-Z0-9_-]+$' -or $BaseCharacter -eq 'kfm-gravitational-lens-test') {throw 'Choose a base character such as kfm.'}
foreach ($lensRequired in @('winmugen.exe','mugen-mod-loader.dll','ALLEG40.DLL','zlib.dll',"chars\$BaseCharacter\$BaseCharacter.def",'data\mugen.cfg')) {if(!(Test-Path -LiteralPath (Join-Path $lensSource $lensRequired))) {throw ("Missing input: $lensRequired")}}
foreach ($lensDll in @('mod.dll','mugen-lens-math.dll')) {if(!(Test-Path -LiteralPath (Join-Path $lensRoot "build\$lensDll"))) {throw 'Build both DLLs first.'}}
$lensWork = Join-Path $lensRoot 'tests\work'
$lensTarget = Join-Path $lensWork $OutputName
if (Test-Path -LiteralPath $lensTarget) { throw 'Existing lens-live-game retained; choose a fresh target before preparing again.' }
New-Item -ItemType Directory -Path $lensTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $lensSource 'winmugen.exe'), (Join-Path $lensSource 'mugen-mod-loader.dll'), (Join-Path $lensSource 'ALLEG40.DLL'), (Join-Path $lensSource 'zlib.dll') -Destination $lensTarget
foreach ($lensFolder in @('chars','data','font','stages','sound','plugins')) {
    Copy-Item -LiteralPath (Join-Path $lensSource $lensFolder) -Destination $lensTarget -Recurse
}
$lensModDirectory = Join-Path $lensTarget 'mods\gravitational-lens'
New-Item -ItemType Directory -Force -Path $lensModDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $lensRoot 'build\mod.dll'), (Join-Path $lensRoot 'build\mugen-lens-math.dll') -Destination $lensModDirectory
[IO.File]::WriteAllText((Join-Path $lensTarget 'mods.txt'), "gravitational-lens`r`n", [Text.Encoding]::ASCII)
$lensCharacter = Join-Path $lensTarget 'chars\kfm-gravitational-lens-test'
if(Test-Path -LiteralPath $lensCharacter) {throw 'Input already contains the diagnostic character. Choose a clean game directory.'}
Copy-Item -LiteralPath (Join-Path $lensSource "chars\$BaseCharacter") -Destination $lensCharacter -Recurse
foreach($lensFile in @('lens-diagnostic.cns','lens-layer-test.cns')) {
    $lensExample=Join-Path $lensRoot "examples\cns\$lensFile"
    Copy-Item -LiteralPath $lensExample -Destination (Join-Path $lensCharacter $lensFile) -Force
}
$lensDefinition = [IO.File]::ReadAllText((Join-Path $lensCharacter "$BaseCharacter.def"))
$lensCns = if($Case -eq 'diagnostic') {'lens-diagnostic.cns'} else {'lens-layer-test.cns'}
if($Case -eq 'max') {
    $lensCns='lens-max-radius.cns'
    $lensCode=[IO.File]::ReadAllText((Join-Path $lensCharacter 'lens-layer-test.cns')).Replace('ID, 180, 18','ID, 512, 18')
    [IO.File]::WriteAllText((Join-Path $lensCharacter $lensCns),$lensCode,[Text.Encoding]::ASCII)
}
if ($lensDefinition -notmatch 'st      = kfm.cns') {throw 'This helper requires the original KFM definition with st      = kfm.cns. Integrate other characters manually.'}
$lensDefinition = $lensDefinition.Replace('st      = kfm.cns', "st      = kfm.cns`r`nst1     = $lensCns")
[IO.File]::WriteAllText((Join-Path $lensCharacter 'lens-test.def'), $lensDefinition, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $lensModDirectory 'verification.enable'),'First-frame QA',[Text.Encoding]::ASCII)
$lensCfg = Join-Path $lensTarget 'data\mugen.cfg'
$lensConfig = [IO.File]::ReadAllText($lensCfg)
$lensConfig = [regex]::Replace($lensConfig,'(?m)^PauseOnDefocus\s*=\s*1\s*$', 'PauseOnDefocus = 0')
[IO.File]::WriteAllText($lensCfg, $lensConfig, [Text.Encoding]::ASCII)
Write-Output "Prepared isolated live case: $lensTarget"

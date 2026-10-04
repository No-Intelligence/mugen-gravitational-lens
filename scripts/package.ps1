$ErrorActionPreference = 'Stop'
$lensRoot = Split-Path -Parent $PSScriptRoot
foreach ($lensDll in @('mod.dll','mugen-lens-math.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $lensRoot "build\$lensDll"))) {
        throw 'Run scripts/build.cmd and build/lens-math-test.exe first.'
    }
}
$lensDist = Join-Path $lensRoot 'dist'
New-Item -ItemType Directory -Path $lensDist -Force | Out-Null
$lensStage = Join-Path $lensDist ('package-' + [guid]::NewGuid().ToString('N'))
$lensMod = Join-Path $lensStage 'mods\gravitational-lens'
New-Item -ItemType Directory -Path $lensMod -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $lensRoot 'build\mod.dll'),(Join-Path $lensRoot 'build\mugen-lens-math.dll') -Destination $lensMod
foreach ($lensFolder in @('docs','include','examples','src','scripts')) {
    Copy-Item -LiteralPath (Join-Path $lensRoot $lensFolder) -Destination $lensStage -Recurse
}
New-Item -ItemType Directory -Path (Join-Path $lensStage 'tests') -Force | Out-Null
foreach ($lensTestFolder in @('native','integration')) {
    Copy-Item -LiteralPath (Join-Path $lensRoot "tests\$lensTestFolder") -Destination (Join-Path $lensStage 'tests') -Recurse
}
Copy-Item -LiteralPath (Join-Path $lensRoot 'README.md'),(Join-Path $lensRoot 'LICENSE') -Destination $lensStage
$lensFiles = @(Get-ChildItem -LiteralPath $lensStage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{
        file = $_.FullName.Substring($lensStage.Length + 1).Replace('\','/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$lensFiles | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $lensStage 'manifest.json') -Encoding utf8
$lensZip = Join-Path $lensDist 'MUGEN-Gravitational-Lens.zip'
if (Test-Path -LiteralPath $lensZip) { throw 'Existing ZIP retained. Rename it before packaging again.' }
Compress-Archive -LiteralPath $lensStage -DestinationPath $lensZip
Write-Output "Created $lensZip"

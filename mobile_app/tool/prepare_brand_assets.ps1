param([string]$Browser = '')
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'prepare_brand_assets.py'))
if ($Browser) { $arguments += @('--browser', $Browser) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Brand asset generation failed.' }

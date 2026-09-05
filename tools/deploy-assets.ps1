param([Parameter(Mandatory=$true)][string]$SourceData,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$sourceRoot=(Resolve-Path -LiteralPath $SourceData).Path
$outputRoot=[IO.Path]::GetFullPath($OutputDirectory)
$dataRoot=Join-Path $outputRoot 'Data'
New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
foreach($folder in @('Modules','Lots')) {
    $destination=Join-Path $dataRoot $folder
    if(Test-Path -LiteralPath $destination) {
        foreach($oldFile in Get-ChildItem -LiteralPath $destination -Filter '*.xml' -File) {
            if(-not (Test-Path -LiteralPath (Join-Path (Join-Path $sourceRoot $folder) $oldFile.Name))) {
                # Preserve retired deployed definitions without loading them as live assets.
                $resolvedOld=$oldFile.FullName
                if(-not $resolvedOld.StartsWith($dataRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset path outside Data' }
                $retired=Join-Path (Join-Path $dataRoot 'RetiredAssets') $folder
                New-Item -ItemType Directory -Force -Path $retired | Out-Null
                Move-Item -LiteralPath $resolvedOld -Destination (Join-Path $retired ($oldFile.BaseName+'-'+[DateTime]::UtcNow.Ticks+'.xml'))
            }
        }
    }
}
Get-ChildItem -LiteralPath $sourceRoot | Where-Object Name -ne 'Generated' | Copy-Item -Destination $dataRoot -Recurse -Force
& (Join-Path $outputRoot 'CityBuilderAssetGenerator.exe') $dataRoot
if($LASTEXITCODE -ne 0) { throw "Asset generation failed with exit code $LASTEXITCODE" }

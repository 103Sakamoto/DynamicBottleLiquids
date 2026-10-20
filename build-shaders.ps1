param([string]$Fxc)
$ErrorActionPreference='Stop'
if(!$Fxc){
 $sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
 $Fxc=Get-ChildItem "$sdk/*/x64/fxc.exe" | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if(!$Fxc -or !(Test-Path -LiteralPath $Fxc)){throw 'Install the Windows SDK or pass -Fxc path/to/fxc.exe'}
$out=Join-Path $PSScriptRoot 'build/shaders'
New-Item -ItemType Directory -Force $out | Out-Null
function Compile($source,$stage,$entry,$output,$defines=@()){
 $arguments=@('/nologo','/WX','/O3','/T',"${stage}_5_0",'/E',$entry,'/Fo',"$out/$output.cso")
 foreach($define in $defines){$arguments+=@('/D',$define)}
 & $Fxc @arguments "$PSScriptRoot/shaders/$source.hlsl"
 if($LASTEXITCODE -ne 0){throw "Shader compilation failed: $output"}
}
Compile OpticalParts vs VSMain OpticalParts.vs
Compile OpticalParts ps PSMain OpticalParts.ps
Compile OpticalParts ps PSMain OpticalParts.direct.ps @('DIRECT_DISTANT=1')
Compile OpticalCompose vs VS OpticalCompose.vs
Compile OpticalCompose ps PS OpticalCompose.ps
Compile OpticalCompose ps PS OpticalCompose.scaled.ps @('UPSCALED_VIEWPORT=1')
Compile OpticalCompose cs CS OpticalCompose.region.cs @('REGION_COMPOSE=1')
Compile OpticalCompose cs CS OpticalCompose.region-scaled.cs @('REGION_COMPOSE=1','UPSCALED_VIEWPORT=1')
Compile RegionBounds cs CS RegionBounds.reduce.cs @('REDUCE=1')
Compile RegionBounds cs CS RegionBounds.finish.cs @('FINISH=1')
Compile RegionBounds cs CS RegionBounds.copy.cs

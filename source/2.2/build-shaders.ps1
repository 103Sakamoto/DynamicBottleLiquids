$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot 'source/desktop/shaders/OpticalParts.hlsl'
$out=Join-Path $PSScriptRoot 'build-common'
$fxc='C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe'
New-Item -ItemType Directory -Force $out | Out-Null
$variants=@(
    @('OpticalParts.ps',''),
    @('OpticalParts.depth-bypass.ps','ENB_DEPTH_BYPASS=1'),
    @('OpticalParts.direct.ps','DIRECT_DISTANT=1'),
    @('OpticalParts.preview.ps','DIRECT_DISTANT=1,INVENTORY_PREVIEW=1'),
    @('OpaquePrototype.forward.ps','DIRECT_DISTANT=1,OPAQUE_PROTOTYPE=1,FORWARD_REFRACTION=1')
)
foreach($variant in $variants){
    $arguments=@('/nologo','/WX','/O3','/T','ps_5_0','/E','PSMain','/Fo',"$out/$($variant[0]).cso")
    if($variant[1]){foreach($define in $variant[1].Split(',')){$arguments+=@('/D',$define)}}
    & $fxc @arguments $source
    if($LASTEXITCODE -ne 0){throw "Shader compilation failed for $($variant[0])"}
}
& $fxc /nologo /WX /O3 /T vs_5_0 /E VSMain /Fo "$out/OpticalParts.vs.cso" $source
if($LASTEXITCODE -ne 0){throw 'Vertex shader compilation failed'}

$ErrorActionPreference='Stop'
$deps=Join-Path $PSScriptRoot '.deps'
New-Item -ItemType Directory -Force $deps | Out-Null
function Checkout($url,$commit,$path){
 if(!(Test-Path -LiteralPath "$path/.git")){
  & git clone --no-checkout $url $path
  if($LASTEXITCODE -ne 0){throw "Clone failed: $url"}
 }
 & git -C $path checkout --detach $commit
 if($LASTEXITCODE -ne 0){throw "Cannot check out pinned dependency: $commit"}
 $actual=& git -C $path rev-parse HEAD
 if($actual -ne $commit){throw "Dependency revision mismatch: $path"}
}
$common=Join-Path $deps 'CommonLibF4'
Checkout 'https://github.com/Dear-Modding-FO4/commonlibf4.git' '01cd60c70a6a2aa79e7fd01b3be577df55fc7235' $common
Checkout 'https://github.com/Dear-Modding-FO4/commonlib-shared.git' '12e1c6a45623f9740a3d866f359d547ef94b5096' "$common/lib/commonlib-shared"
Checkout 'https://github.com/Dear-Modding-FO4/DearModdingUI-API.git' '04b1f40683c0b11bcd696c8df76db5157ec3a3a8' "$common/lib/dearmoddingui-api"

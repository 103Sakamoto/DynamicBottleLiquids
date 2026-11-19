Texture2D<float4> Before : register(t14);
Texture2D<float> Mask : register(t15);
float4 VS(uint id:SV_VertexID):SV_Position {
 float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
float4 PS(float4 p:SV_Position):SV_Target {
 int3 pixel=int3(p.xy,0);float coverage=Mask.Load(pixel);
#ifndef RESTORE_REFERENCE
 // Exact zero only: preserve even the faintest nonzero glass protection.
 [branch] if(coverage==0)discard;
#endif
 return float4(Before.Load(pixel).rgb,coverage);
}

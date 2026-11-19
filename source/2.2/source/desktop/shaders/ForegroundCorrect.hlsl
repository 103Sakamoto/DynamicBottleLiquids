Texture2D<float4> Local:register(t0);
Texture2D<float4> Transmission:register(t1);
Texture2D<float4> Foreground:register(t2);
Texture2D<float4> NativeFiltered:register(t3);
SamplerState Linear:register(s0);
cbuffer Post:register(b0){float4 Intensity;}
cbuffer Mapping:register(b1){float4 BottleViewport;float4 NativeViewport;float4 BufferSizes;float4 BlendFactor;}
struct VSOut{float4 position:SV_Position;float2 uv:TEXCOORD0;};
VSOut VS(uint id:SV_VertexID){VSOut o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 PS(VSOut v):SV_Target{
 int2 p=int2(v.position.xy);float4 light=Local.Load(int3(p,0));
 float3 t=Transmission.Load(int3(p,0)).rgb;
 if(all(t==0)&&light.a==0)return light;
 float2 uv=(v.position.xy-BottleViewport.xy)/BottleViewport.zw;
 if(any(uv<0)||any(uv>1))return light;
 float2 at=(NativeViewport.xy+uv*NativeViewport.zw)/BufferSizes.xy;
 float3 foreground=max(Foreground.SampleLevel(Linear,at,0).rgb,0);
 // Cap to the native filtered total to avoid adding more than the scene contains.
 float3 total=max(NativeFiltered.SampleLevel(Linear,at,0).rgb,0);
 foreground=min(foreground,total)*Intensity.rgb*BlendFactor.rgb;
 light.rgb+=(1-saturate(t))*foreground;
 return light;
}

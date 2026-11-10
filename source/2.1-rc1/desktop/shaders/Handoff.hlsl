Texture2D<float4> Source:register(t0);
SamplerState LinearClamp:register(s0);
cbuffer Exposure:register(b13){float4 ExposureMultiplier;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V VS(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 PS(V v):SV_Target0{return Source.SampleLevel(LinearClamp,v.uv,0)*ExposureMultiplier.x;}

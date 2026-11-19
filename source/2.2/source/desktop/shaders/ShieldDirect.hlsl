// Audited bilinear SDK application only. Keep the unused input to preserve v2 UVs.
cbuffer NativePass:register(b0){float4 FrameData[14];};
cbuffer NativePost:register(b2){float4 Intensity;};
cbuffer ShieldRegion:register(b13){int4 Region;};
Texture2D<float4> Godrays:register(t0);
SamplerState Bilinear:register(s1);
Texture2D<float> ExtinctionMask:register(t15);
float4 main(float4 position:SV_Position,float4 unused:TEXCOORD0,float2 uv:TEXCOORD1):SV_Target0 {
 float2 sampleUV=uv*FrameData[13].xy*FrameData[12].zw;
 float3 lighting=Godrays.SampleLevel(Bilinear,sampleUV,0).rgb*Intensity.rgb;
 int2 p=int2(position.xy);
 // Mask is cleared only inside this frame's union rectangle. Never read stale pixels.
 float extinction=0;
 [branch] if(all(p>=Region.xy)&&all(p<Region.zw))extinction=saturate(ExtinctionMask.Load(int3(p,0)));
 return float4(lighting*(1-extinction),1);
}

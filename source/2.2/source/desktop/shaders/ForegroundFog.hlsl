// Private native-volume replay. Main scene depth and native god rays are unchanged.
cbuffer NativePass:register(b0){float4 P[21];}
cbuffer Volume:register(b1){float4 V[46];}
cbuffer Mapping:register(b3){float4 BottleViewport;float4 NativeViewport;}
Texture1D<float4> Phase:register(t4);
Texture2DMS<float> Depth:register(t2);
Texture2D<float> BottleDepth:register(t22);
SamplerState Bilinear:register(s1);
float Limit(float2 position){
 float2 uv=(position-NativeViewport.xy)/NativeViewport.zw;
 int2 p=int2(BottleViewport.xy+uv*BottleViewport.zw);
 uint w,h;BottleDepth.GetDimensions(w,h);p=clamp(p,int2(0,0),int2(w,h)-1);
 float z=BottleDepth.Load(int3(p,0));
 // Conservative 2x2 footprint for the native half-resolution volume target.
 z=min(z,BottleDepth.Load(int3(min(p+int2(1,0),int2(w,h)-1),0)));
 z=min(z,BottleDepth.Load(int3(min(p+int2(0,1),int2(w,h)-1),0)));
 z=min(z,BottleDepth.Load(int3(min(p+1,int2(w,h)-1),0)));
 clip(.9999999-z);
 float2 xy=uv*float2(2,-2)+float2(-1,1);
 float4 world=xy.y*P[9]+xy.x*P[8]+z*P[10]+P[11];
 return length(world.xyz/world.w-P[14].xyz);
}
float3 Integral(float3 ray,float limit){
 float d=length(ray);float3 dir=ray/max(d,1e-8);
 float x=clamp(-dot(dir,V[38].xyz),-1,1),a=abs(x);
 float angle=((-.018729*a+.074261)*a-.212114)*a+1.570729;
 angle*=sqrt(1-a);angle=x<0?3.141593-angle:angle;
 float3 phase=Phase.SampleLevel(Bilinear,angle*.318310,0).rgb;
 float3 sigma=max(P[18].xyz+P[19].xyz,0);float distance=min(d,limit);
 float3 q=1-exp(-sigma*distance);
 float3 integral=float3(sigma.x>1e-8?q.x/sigma.x:distance,sigma.y>1e-8?q.y/sigma.y:distance,sigma.z>1e-8?q.z/sigma.z:distance);
 return phase*phase*max(P[19].xyz,0)*integral*V[40].xyz;
}
float4 Boundary(float4 position:SV_Position,float4 world:TEXCOORD0,bool front:SV_IsFrontFace):SV_Target{
 return float4(Integral(world.xyz-P[14].xyz,Limit(position.xy))*(front?-1:1),0);
}
float4 Endpoint(float4 position:SV_Position,float4 unused:TEXCOORD0,float2 uv:TEXCOORD1,uint sample:SV_SampleIndex):SV_Target{
 float2 xy=position.xy*(P[13].zw*P[14].w)*float2(2,-2)+float2(-1,1);
 float z=Depth.Load(int2(position.xy),sample);
 float4 world=xy.y*P[9]+xy.x*P[8]+z*P[10]+P[11];
 return float4(Integral(world.xyz/world.w-P[14].xyz,Limit(position.xy)),0);
}

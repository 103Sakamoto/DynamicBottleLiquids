Texture2D<float4> Background:register(t0);
Texture2D<float4> LocalLight:register(t1);
Texture2D<float4> Transmission:register(t2);
Texture2D<float4> Distortion:register(t3);
Texture2D<float> SceneDepth:register(t4);
Texture2D<float> BottleDepth:register(t5);
SamplerState LinearClamp:register(s0);
#ifdef UPSCALED_VIEWPORT
cbuffer ActiveViewport:register(b11){float2 ActiveExtent;float2 Reserved;};
#endif
struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;};
V VS(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 PS(V v):SV_Target0 {
    uint2 pixel=uint2(v.position.xy);
    float4 unchanged=Background.SampleLevel(LinearClamp,v.uv,0);
    #ifdef UPSCALED_VIEWPORT
    if(any(v.uv>=ActiveExtent))return unchanged;
#endif
    uint w,h;SceneDepth.GetDimensions(w,h);
    float sceneZ=SceneDepth.Load(int3(pixel,0));
    float4 d=Distortion.Load(int3(pixel,0));if(d.a==0)return unchanged;
    float4 t=Transmission.Load(int3(pixel,0));
    float3 local=LocalLight.Load(int3(pixel,0)).rgb;
    float surfaceDepth=BottleDepth.Load(int3(pixel,0));
    if(sceneZ+1e-7<surfaceDepth)return unchanged;
    // FO4 native initial displacement and screen-edge policy, in UV coordinates.
    float2 shifted=v.uv+float2(-1,1)*.25*d.b*(d.rg-.5);
    #ifdef UPSCALED_VIEWPORT
    shifted/=ActiveExtent;
#endif
    float2 edge=shifted;
    edge=lerp(edge,(edge-.85)*.78+.85,step(.85,shifted));
    edge=lerp(edge,.15-(.15-shifted)*.78,1-step(.15,shifted));
    shifted=lerp(shifted,edge,d.b);
#ifdef UPSCALED_VIEWPORT
    shifted*=ActiveExtent;
#endif
    // Exact identity only, after native edge handling and the original foreground guard.
    // No epsilon: small nonzero displacements retain the original corridor checks.
    [branch] if(all(shifted==v.uv))
        return float4(local+t.rgb*unchanged.rgb,unchanged.a);

    bool valid=all(shifted>=0)&&all(shifted<=1);
#ifdef UPSCALED_VIEWPORT
    // Bilinear footprint must stay inside the populated render region.
    valid=valid&&all(shifted>=.5/float2(w,h))&&all(shifted<=ActiveExtent-.5/float2(w,h));
#endif
    // Coverage belongs to the destination pixel. A cylinder lens may sample
    // background beyond the bottle silhouette; scene depth validates that ray.
    // Use the existing native depth, including intervening foreground coverage.
    [unroll]for(int i=1;i<=8;i++){
        float2 q=lerp(v.uv,shifted,i/8.0);
        int2 p=clamp(int2(q*float2(w,h)),int2(0,0),int2(w-1,h-1));
        if(SceneDepth.Load(int3(p,0))+1e-7<surfaceDepth)valid=false;
    }
    float3 scene=valid?Background.SampleLevel(LinearClamp,shifted,0).rgb:unchanged.rgb;
    return float4(local+t.rgb*scene,unchanged.a);
}

#ifdef REGION_COMPOSE
ByteAddressBuffer RegionArgs:register(t6);
RWTexture2D<float4> RegionOutput:register(u0);
[numthreads(8,8,1)]void CS(uint3 id:SV_DispatchThreadID){
 uint4 r=RegionArgs.Load4(12);uint2 p=r.xy+id.xy;
 if(any(p>=r.zw))return;
 uint w,h;Background.GetDimensions(w,h);V v;v.position=float4(float2(p)+.5,0,1);v.uv=(float2(p)+.5)/float2(w,h);
 RegionOutput[p]=PS(v);
}
#endif

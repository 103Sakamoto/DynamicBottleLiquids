// Vanilla Effect PerFrame layout, shared by the native colour and utility passes.
// Keep the distance/height ramps in the engine's camera-relative coordinate frame.
cbuffer NativeFogFrame : register(b12) { float4 FogScene[47]; };
float4 NativeBottleFog(float4 clipPosition) {
    float3 viewPosition=float3(dot(FogScene[20],clipPosition),dot(FogScene[21],clipPosition),dot(FogScene[22],clipPosition));
    float ramp=length(viewPosition)*FogScene[41].x-FogScene[41].z;
    float distance=saturate(ramp);
    // Also preserves the historical appearance when b12 is unbound in tools.
    if(distance<=0)return 0;
    float height=dot(FogScene[14],float4(viewPosition,1))+FogScene[35].z;
    float2 heightRamp=saturate(height*FogScene[46].xy-FogScene[46].zw);
    float heightWeight=lerp(heightRamp.x,heightRamp.y,distance);
    float density=lerp(1,FogScene[44].w,heightWeight);
    float limit=FogScene[43].w;
    if(ramp>.75)limit=min(lerp(limit,1,(distance-.75)*4),1);
    float curve=min(pow(distance,FogScene[42].w),limit);
    float nearFade=ramp<.015?distance*66.666672:1;
    float3 low=lerp(FogScene[42].xyz,FogScene[44].xyz,curve);
    float3 high=lerp(FogScene[43].xyz,FogScene[45].xyz,curve);
    return float4(lerp(low,high,heightWeight),saturate(curve*density*nearFade));
}


// Diagnostic build retains native fog shading only; no fog culling policy.

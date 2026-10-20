// Effect envmap composition verified against the captured vanilla PS:
// build/audit-refraction/baseline10461/ShaderStage.Pixel-21003.txt, ops 20-65.
// These are the original colour draw's b2 values already snapshotted at b12.
cbuffer SurfaceNativeLighting : register(b12) {
    float4 NativeLightPositionX,NativeLightPositionY,NativeLightPositionZ;
    float4 NativeSpotDirectionX,NativeSpotDirectionY,NativeSpotDirectionZ;
    float4 NativeSpotExponent,NativeSpotCosHalfPhi,NativeRadiusInverse;
    float4 NativeLightRed,NativeLightGreen,NativeLightBlue;
    float4 NativeDirectionalColour,NativePropertyColour,NativeAlphaTest;
};
SamplerState SurfaceCubeSampler : register(s5);

float3 effectSurfaceLighting(float3 localPosition) {
    if(SurfaceEnvironment.z<.5)return NativePropertyColour.rgb;
    float3 meshPosition=mul(SimulationToMesh,float4(localPosition,1));
    float4 x=meshPosition.x-NativeLightPositionX;
    float4 y=meshPosition.y-NativeLightPositionY;
    float4 z=meshPosition.z-NativeLightPositionZ;
    float4 distance=sqrt(x*x+y*y+z*z);
    float4 fade=saturate(distance*NativeRadiusInverse);
    fade=pow(1-fade*fade,2.2);
    distance=max(distance,.001);
    float4 spot=saturate((x*NativeSpotDirectionX+y*NativeSpotDirectionY+z*NativeSpotDirectionZ)/distance);
    spot=saturate(1-(1-spot)/(1-NativeSpotCosHalfPhi));
    float4 spotPower=float4(
        NativeSpotExponent.x!=0?min(pow(spot.x,NativeSpotExponent.x),1):1,
        NativeSpotExponent.y!=0?min(pow(spot.y,NativeSpotExponent.y),1):1,
        NativeSpotExponent.z!=0?min(pow(spot.z,NativeSpotExponent.z),1):1,
        NativeSpotExponent.w!=0?min(pow(spot.w,NativeSpotExponent.w),1):1);
    fade*=spotPower;
    return NativeDirectionalColour.rgb+float3(dot(fade,NativeLightRed),dot(fade,NativeLightGreen),dot(fade,NativeLightBlue));
}

float3 surfaceEnvironment(float3 nativeEye,float3 nativeNormal,float3 localPosition,float2 spec,float normalAlpha,float baseAlpha,float falloff) {
    // Captured native Effect ops 16-20: E - 2*N*dot(N,E). Its cube convention
    // is view based. Do not reuse/negate the corrected WORLD liquid ray here.
    float3 direction=reflect(nativeEye,nativeNormal);
    float3 reflected;
    [branch] if(SurfaceResponse.z>.5){
        // Converted BGSM glass: preserve its R spec/G smoothness controls.
        // Uniform reflection strength; no extra view-angle Fresnel multiplier.
        float smoothness=saturate(spec.y*SurfaceResponse.x);
        float3 cube=Environment.SampleLevel(SurfaceCubeSampler,direction,(1-smoothness)*6).rgb;
        reflected=cube*SurfaceEnvironment.x*spec.x*SurfaceSpecular.w*SurfaceSpecular.rgb*.4;
    }else{
        // Authored Effect ops 20-24, then native source-alpha/falloff weighting.
        // Reflection never inherits liquid absorption, opacity or distance fade.
        float3 cube=Environment.Sample(SurfaceCubeSampler,direction).rgb;
        float alpha=saturate(baseAlpha*SurfaceAlpha.x*NativePropertyColour.w*falloff);
        reflected=cube*SurfaceEnvironment.x*spec.x*normalAlpha*alpha;
    }
    return reflected*lerp(float3(1,1,1),effectSurfaceLighting(localPosition),SurfaceEnvironment.y);
}

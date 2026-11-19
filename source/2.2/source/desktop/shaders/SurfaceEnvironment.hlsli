// Shared glass response. Native material intensity/tint/lighting do not scale
// the cubemap. Adapters preserve texture channels, not legacy scalar gains.
SamplerState SurfaceCubeSampler : register(s5);
float sharedGlassFresnel(float cosine) {
    float grazing=1-saturate(abs(cosine));
    return .04+.96*grazing*grazing*grazing*grazing*grazing;
}
float sharedGlassMask(float2 spec,float normalAlpha,bool bgsm) {
    return saturate(spec.x)*(bgsm?1:saturate(normalAlpha));
}
float3 surfaceEnvironment(float3 nativeEye,float3 nativeNormal,float3 localPosition,float2 spec,float normalAlpha,float baseAlpha,float falloff,float smoothnessOverride=-1) {
    float3 eye=normalize(nativeEye),normal=normalize(nativeNormal);
    float3 direction=reflect(eye,normal);
    bool bgsm=SurfaceResponse.z>.5;
    // BGSM packs smoothness in G; authored Effect masks have no shared G contract.
    float smoothness=bgsm?saturate(spec.y*SurfaceResponse.x):1;
    if(smoothnessOverride>=0)smoothness=saturate(smoothnessOverride);
    float3 cube=Environment.SampleLevel(SurfaceCubeSampler,direction,(1-smoothness)*6).rgb;
    float coverage=saturate(baseAlpha);
#ifdef INVENTORY_PREVIEW
    cube*=1.75; // Inventory-only presentation multiplier; world glass stays 1x.
#endif
    return cube*SurfaceEnvironment.x*sharedGlassFresnel(dot(eye,normal))
        *sharedGlassMask(spec,normalAlpha,bgsm)*coverage;
}

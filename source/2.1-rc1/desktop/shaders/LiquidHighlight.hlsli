float liquidNoiseMotion(float primary,float secondary){return smoothstep(.0005,.03,abs(primary)+abs(secondary));}
// Shared by the actual native wave field and its surface detail. The signed
// mode gains affect amplitude; both use the same phase/frequency/time here.
#define LIQUID_SHARED_WAVE_PHASES
float2 liquidWavePhases(float3 p,float frequency,float time){
    return float2(dot(p,float3(.8,.6,0))*frequency+time*3,
        dot(p,float3(-.48,.64,.6))*frequency*1.73-time*5.1);
}
float2 liquidSurfaceNoiseUV(float3 p,float4 wave){
    float frequency=max(abs(wave.y),.0001);
    return liquidWavePhases(p,frequency,wave.z)*float2(.055/frequency,.055/(frequency*1.73));
}
float liquidSurfaceNoiseStrength(float4 wave,float4 detail){
    return liquidNoiseMotion(wave.x*detail.y,detail.x*detail.z)*saturate(detail.w);
}
float3 liquidSurfaceNoiseSlope(float3 up,float2 slope,float4 wave,float4 detail){
    float2 amplitude=float2(wave.x*detail.y,detail.x*detail.z);
    float2 weight=amplitude/max(length(amplitude),.0001);
    float3 perturbation=float3(.8,.6,0)*slope.x*weight.x
        +float3(-.48,.64,.6)*slope.y*weight.y;
    // Project onto the actual liquid plane, including tilted bottles. No
    // arbitrary fixed bitangent or a basis pole when the liquid tilts.
    return perturbation-up*dot(up,perturbation);
}
float liquidSurfaceRoughness(float4 wave,float4 detail,float variation){
    float2 waveSlope=float2(wave.x*detail.y,detail.x*detail.z)*wave.y*float2(1,1.73);
    return clamp(.18+length(waveSlope)*.5+sqrt(max(variation,0))*.2,.18,.45);
}
// Bounded GGX liquid highlight. Inputs and output are linear scene lighting.
float3 liquidHighlight(float3 N,float3 V,float3 L,float3 irradiance,float ior,float roughness){
    float nv=saturate(dot(N,V)),nl=saturate(dot(N,L));
    float3 h=V+L;float h2=dot(h,h);
    if(nv<=0 || nl<=0 || h2<1e-6)return 0;
    h*=rsqrt(h2);
    float nh=saturate(dot(N,h)),vh=saturate(dot(V,h));
    float a=max(roughness*roughness,.025),a2=a*a;
    float d=nh*nh*(a2-1)+1;
    float D=a2/(3.14159265*d*d);
    float gv=2*nv/(nv+sqrt(a2+(1-a2)*nv*nv));
    float gl=2*nl/(nl+sqrt(a2+(1-a2)*nl*nl));
    float f0=(ior-1)/(ior+1);f0*=f0;
    float F=f0+(1-f0)*pow(1-vh,5);
    // Cap the narrow peak to avoid exposure spikes on subpixel surfaces.
    return max(irradiance,0)*min(D*gv*gl*F/max(4*nv,1e-5),1.0);
}

// Straight camera-ray intersection and optical extinction. The SDK accepts only
// depth: fractional extinction is represented by spatial sample coverage.
bool LiquidFrontExtinction(float3 local,float3 eye,out float3 hit,out float extinction) {
    hit=0;extinction=0;
    if(Trace.y<=.000001)return false;
    float3 direction=normalize(local-eye);
    int steps=colorTraceSteps(),first,last;
    float stepSize=Trace.x/steps,thickness=0,firstT=0;
    traceRange(local,direction,steps,first,last);
    float t=first*stepSize-Trace.x*.5;
    float2 previous=boundaryFields(local+direction*t);
    bool found=false;
    [loop] for(int j=first;j<last;j++){
        float2 next=boundaryFields(local+direction*(t+stepSize));
        float enter=0,leave=1;
        clipBoundary(previous.x,next.x,enter,leave);
        if(Trace.y<1)clipBoundary(previous.y,next.y,enter,leave);
        if(leave>enter){
            if(!found){firstT=t+enter*stepSize;found=true;}
            thickness+=(leave-enter)*stepSize;
            if(Absorption.w>=.99999 && thickness>=.15){
                hit=local+direction*firstT;
                extinction=1;
                return dot(hit-eye,direction)>0;
            }
        }
        previous=next;t+=stepSize;
    }
    if(!found||thickness<.15)return false;
    hit=local+direction*firstT;
    float3 transmission=exp(-max(Absorption.rgb,0)*thickness)*(1-saturate(Absorption.w));
    extinction=saturate(1-dot(transmission,float3(.2126,.7152,.0722)));
    return extinction>0 && dot(hit-eye,direction)>0;
}

// Stable screen-space coverage with a per-object offset so overlapping bottles
// do not all select exactly the same pixels. No frame-varying noise or shield.
float LiquidDepthCoverageThreshold(float2 pixel) {
    float seed=dot(float3(MeshToWorld[0].w,MeshToWorld[1].w,MeshToWorld[2].w),float3(.1031,.11369,.13787));
    float2 offset=floor(frac(float2(seed,seed*1.61803))*4096);
    return frac(52.9829189*frac(dot(floor(pixel)+offset,float2(.06711056,.00583715))));
}

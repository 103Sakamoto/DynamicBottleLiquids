// Analytic liquid-cylinder lens. Two Snell interfaces, no marching or textures.
// Radius is the local profile radius; shoulders, caps and rectangular bottles
// remain approximations. Background distance is estimated at four radii.
bool liquidLensRay(float3 origin,float3 incident,float radius,float ior,out float3 exitPoint,out float3 outgoing) {
    exitPoint=origin;outgoing=incident;
    float a=dot(incident.xy,incident.xy);
    if(a<.04 || radius<=.001 || ior<=1.0001)return false;
    float t=-dot(origin.xy,incident.xy)/a;
    float3 closest=origin+incident*t;
    float h2=(radius*radius-dot(closest.xy,closest.xy))/a;
    if(h2<=0)return false;
    float3 entry=closest-incident*sqrt(h2);
    float3 inside=refract(incident,float3(entry.xy/radius,0),1/ior);
    float b=dot(inside.xy,inside.xy);
    if(b<1e-6)return false;
    float travel=-2*dot(entry.xy,inside.xy)/b;
    exitPoint=entry+inside*travel;
    outgoing=refract(inside,-float3(exitPoint.xy/radius,0),ior);
    if(dot(outgoing,outgoing)<.5)return false;
    return true;
}

bool liquidLensTarget(float3 origin,float3 incident,float radius,float ior,out float3 target) {
    float3 exitPoint,outgoing;
    bool valid=liquidLensRay(origin,incident,radius,ior,exitPoint,outgoing);
    target=valid?exitPoint+outgoing*(4*radius):origin;
    return valid;
}

// Shared algorithm compiled as GLSL ES 3.0 and HLSL SM5 via type aliases.
// Positions, absorption lengths, cavity radii and wave amplitude use NIF local units.
// Prototype uses a procedural studio environment; game integration must supply scene lighting.
float cavityRadius(float z) {
    if(z<uRings[0].x || z>uRings[uRingCount-1].x) return 0.0;
    // Profiles have 2..16 strictly ascending rings. Find the first upper
    // endpoint >= z in at most four comparisons, preserving the old interval
    // choice at exact ring heights and the original interpolation arithmetic.
    int low=1,high=uRingCount-1;
    for(int search=0;search<4;++search){
        if(low>=high)break;
        int mid=(low+high)>>1;
        if(z>uRings[mid].x)low=mid+1;else high=mid;
    }
    float t=(z-uRings[low-1].x)/(uRings[low].x-uRings[low-1].x);
    return mix(uRings[low-1].y,uRings[low].y,t);
}
float waveHeight(vec3 p) {
#ifdef SECONDARY_WAVE_AMPLITUDE
    float secondary=SECONDARY_WAVE_AMPLITUDE;
#ifdef LIQUID_SHARED_WAVE_PHASES
    vec2 phase=liquidWavePhases(p,uWave.y,uWave.z);
    return uWave.x*PRIMARY_WAVE_GAIN*sin(phase.x)+secondary*SECONDARY_WAVE_GAIN*sin(phase.y);
#else
    return uWave.x*PRIMARY_WAVE_GAIN*sin(dot(p,vec3(.8,.6,0.0))*uWave.y+uWave.z*3.0)
        +secondary*SECONDARY_WAVE_GAIN*sin(dot(p,vec3(-.48,.64,.6))*uWave.y*1.73-uWave.z*5.1);
#endif
#else
    // Legacy standalone preview retains its matching host-side wave equation.
    return uWave.x*(sin(p.x*uWave.y+uWave.z*3.0)+.5*sin(p.y*uWave.y*1.37-uWave.z*2.3));
#endif
}
float meniscusFromWall(float wall) {
    // Cubic contact rise: flat interior joins smoothly, finite slope at glass.
    // Keep the 0.06 maximum shared by conservative tracing bounds.
    float t=clamp(1.0-wall/0.35,0.0,1.0);
    return 0.06*t*t*t;
}
float meniscusHeight(vec3 p) {
    float bottom=uRings[0].x,top=uRings[uRingCount-1].x;
    float wall=min(cavityRadius(clamp(p.z,bottom,top))-length(p.xy),min(p.z-bottom,top-p.z));
    return meniscusFromWall(wall);
}
float liquidDistance(vec3 p) { return dot(p,uPlane.xyz)-uPlane.w-waveHeight(p)-meniscusHeight(p); }
// Endpoint fields: negative inside the cavity / below the liquid surface.
// Clamp radius lookup to the end rings so cap crossings interpolate continuously.
vec2 boundaryFields(vec3 p) {
    float bottom=uRings[0].x, top=uRings[uRingCount-1].x;
    float radial=length(p.xy)-cavityRadius(clamp(p.z,bottom,top));
    float cavity=max(radial,max(bottom-p.z,p.z-top));
    return vec2(cavity,dot(p,uPlane.xyz)-uPlane.w-waveHeight(p)-meniscusFromWall(-cavity));
}
void clipBoundary(float a,float b,inout float enter,inout float leave) {
    if(a>0.0 && b>0.0){leave=enter;return;}
    if(a>0.0)enter=max(enter,a/(a-b));
    else if(b>0.0)leave=min(leave,a/(a-b));
}
float boundaryFraction(vec2 a,vec2 b) {
    if(uTrace.y<=0.0)return 0.0;
    float enter=0.0,leave=1.0;
    clipBoundary(a.x,b.x,enter,leave);
    if(uTrace.y<1.0)clipBoundary(a.y,b.y,enter,leave);
    return max(leave-enter,0.0);
}
vec3 studio(vec3 d) {
#ifdef NUKA_SCENE_TRANSMISSION
    // Game lighting is applied by the adapter; no self-lit studio environment.
    return vec3(0.0,0.0,0.0);
#else
    vec3 c = mix(vec3(0.018,0.025,0.031), vec3(0.14,0.19,0.21), smoothstep(-0.65,0.8,d.z));
    vec3 horizontal=vec3(d.x,d.y,0.0)/max(length(d.xy),0.0001);
    float a = pow(max(dot(horizontal,normalize(vec3(-0.6,-0.8,0.0))),0.0),100.0);
    float b = pow(max(dot(horizontal,normalize(vec3(0.95,0.31,0.0))),0.0),150.0);
    float extent=1.0-smoothstep(0.65,0.95,abs(d.z));
    return c + extent*(vec3(1.0,0.88,0.7)*a*5.0 + vec3(0.4,0.8,1.0)*b*4.0);
#endif
}
float fresnel(float cosTheta,float ior) {
    float f=(ior-1.0)/(ior+1.0); f*=f;
    return f+(1.0-f)*pow(1.0-clamp(cosTheta,0.0,1.0),5.0);
}
vec3 shadeBottleParts(vec3 p,vec3 normal,vec3 eye,out vec3 transmission,out float liquidPresence,out vec4 surfaceHit,out float liquidPath) {
    liquidPath=0.0;
    surfaceHit=vec4(0.0,0.0,0.0,0.0);
    transmission=vec3(0.0,0.0,0.0);liquidPresence=0.0;
    vec3 incoming=normalize(p-eye), N=normalize(normal);
    if(dot(N,incoming)>0.0) N=-N;
    float f=fresnel(dot(N,-incoming),uOptics.x);
    vec3 reflected=studio(reflect(incoming,N));
    if(p.z>uCapZ) {
        return vec3(0.29,0.035,0.025)*(0.25+0.75*max(dot(N,normalize(vec3(-1.0,-1.0,2.0))),0.0))+reflected*0.18;
    }
    // Effective air-to-liquid bending is intentionally approximate (no internal reflection tracing).
#ifdef NUKA_SCENE_TRANSMISSION
    vec3 dir=Optics.w>0.0?incoming:normalize(mix(incoming,refract(incoming,N,1.0/uOptics.y),clamp(1.0-BubblePadding.y,0.0,1.0)));
#else
    vec3 dir=refract(incoming,N,1.0/uOptics.y);
#endif
    vec3 transmitted=studio(dir);
#ifdef NUKA_SCENE_TRANSMISSION
    transmitted=vec3(0.0,0.0,0.0);
#endif
    float lengthLiquid=0.0, topLight=0.0;
    float straightLength=0.0;
    int steps=TRACE_STEPS;
#ifdef NUKA_SCENE_TRANSMISSION
    steps=colorTraceSteps();
#endif
    float dt=uTrace.x/float(steps);
    int first=0,last=steps;
#ifdef NUKA_SCENE_TRANSMISSION
    [branch] if(uTrace.y>0.0){
    bool bent=Optics.w<=0.0 && BubblePadding.y<1.0;
    traceRange(p,dir,steps,first,last);
    if(bent){
        int straightFirst,straightLast;
        traceRange(p,incoming,steps,straightFirst,straightLast);
        first=min(first,straightFirst);last=max(last,straightLast);
    }
#endif
#ifdef NUKA_SCENE_TRANSMISSION
    // Reuse segment endpoints: one new field evaluation per ray and step.
    vec2 previous=boundaryFields(p+dir*(float(first)*dt-uTrace.x*0.5));
    vec2 previousStraight=previous;
    bool foundSurface=false;
    if(bent)previousStraight=boundaryFields(p+incoming*(float(first)*dt-uTrace.x*0.5));
    for(int j=first;j<last;++j){
        float t=float(j+1)*dt-uTrace.x*0.5;
        vec2 next=boundaryFields(p+dir*t);
        vec2 surfaceA=previous,surfaceB=next;
        if(bent){surfaceA=previousStraight;surfaceB=boundaryFields(p+incoming*t);}
        if(!foundSurface && uTrace.y<1.0 && surfaceA.y>0.0 && surfaceB.y<=0.0){
            float crossing=surfaceA.y/(surfaceA.y-surfaceB.y);
            vec3 hit=p+incoming*(t-dt+crossing*dt);
            if(boundaryFields(hit).x<=0.0){
                surfaceHit=vec4(hit,exp(-max(uAbsorption.x,max(uAbsorption.y,uAbsorption.z))*straightLength));
                foundSurface=true;
            }
        }
        lengthLiquid+=boundaryFraction(previous,next)*dt;
        previous=next;
        if(bent){
            vec2 nextStraight=surfaceB;
            straightLength+=boundaryFraction(previousStraight,nextStraight)*dt;
            previousStraight=nextStraight;
        }
        else straightLength=lengthLiquid;
    }
    lengthLiquid=max(lengthLiquid,straightLength);
    }
#else
    for(int j=first;j<last;++j) {
        #ifdef NUKA_SCENE_TRANSMISSION
        // Screen-space refraction must not reveal scenery through the opaque
        // liquid on the original sightline when its bent ray misses the cavity.
        vec3 straight=p+incoming*((float(j)+0.5)*dt-uTrace.x*0.5);
        float sr=Optics.w>0.0?0.0:cavityRadius(straight.z);
        if(sr>0.0 && dot(straight.xy,straight.xy)<sr*sr && uTrace.y>0.0){
            float span=max(abs(dot(incoming,uPlane.xyz))*dt,0.07);
            straightLength+=(uTrace.y>=1.0?1.0:clamp(0.5-liquidDistance(straight)/span,0.0,1.0))*dt;
        }
#endif
        #ifdef NUKA_SCENE_TRANSMISSION
        // The source glass can rasterize its far shell. Trace both sides of
        // that shell point rather than assuming it is the ray entry point.
        vec3 q=p+dir*((float(j)+0.5)*dt-uTrace.x*0.5);
#else
        vec3 q=p+dir*((float(j)+0.5)*dt);
#endif
        float r=cavityRadius(q.z);
        if(r>0.0 && dot(q.xy,q.xy)<r*r) {
            float d=liquidDistance(q);
            // Integrate a linear surface crossing over this ray segment, instead
            // of counting entire samples. This removes contour bands at the meniscus.
            float halfSpan=max(abs(dot(dir,uPlane.xyz))*dt*0.5,0.035);
            float inside=clamp(0.5-d/(2.0*halfSpan),0.0,1.0);
            if(uTrace.y<=0.0) inside=0.0;
            if(uTrace.y>=1.0) inside=1.0;
            lengthLiquid+=inside*dt;
            if(uTrace.y>0.0 && uTrace.y<1.0) topLight=max(topLight,1.0-smoothstep(0.0,max(halfSpan,0.035),abs(d)));
        }
    }
    #ifdef NUKA_SCENE_TRANSMISSION
    lengthLiquid=max(lengthLiquid,straightLength);
#endif
#endif
    liquidPath=lengthLiquid;
    liquidPresence=smoothstep(0.0,0.15,lengthLiquid);
    vec3 attenuation=exp(-uAbsorption*lengthLiquid);
#ifdef NUKA_SCENE_TRANSMISSION
    // Opacity is a coverage control in addition to optical depth. An opaque
    // profile has exactly zero transmission once the ray enters the liquid.
    attenuation*=1.0-Absorption.w*smoothstep(0.0,0.15,lengthLiquid);
#endif
    vec3 liquid=transmitted*attenuation+uScatter*(vec3(1.0,1.0,1.0)-attenuation);
#ifdef NUKA_SCENE_TRANSMISSION
    // Finite-slab approximation: reuse the view-ray thickness to estimate a
    // representative incident path. Beer extinction is independent of the
    // artistic opaque-coverage control, which previously flattened all shading.
    if(Scatter.w>0.0){
    vec3 sigma=min(uAbsorption,vec3(2.0,3.0,4.0));
    vec3 pathTransmission=exp(-sigma*lengthLiquid*0.35);
    vec3 slabScatter=uScatter*(vec3(1.0,1.0,1.0)-attenuation)*(vec3(0.12,0.12,0.12)+0.88*pathTransmission)
        *(1.0-exp(-lengthLiquid*8.0));
    liquid=mix(liquid,slabScatter,Scatter.w);
    }
#endif
#ifndef NUKA_SCENE_TRANSMISSION
    liquid+=uEmission*(1.0-exp(-lengthLiquid*0.15));
#endif
    // Analytic ray/sphere intersections avoid missing small bubbles between ray-march steps.
    // Centers are advanced against gravity and clipped to the liquid by the CPU.
    vec3 bubbleLight=vec3(0.0,0.0,0.0);
#ifndef BUBBLE_CAPACITY
#define BUBBLE_CAPACITY 96
#endif
    for(int k=0;k<BUBBLE_CAPACITY;++k) {
        if(k>=uBubbleCount) break;
#ifdef BUBBLE_GROUP_CULL
        if((k&7)==0){
            vec4 bound=BubbleGroups[k>>3];
            // A zero bound permits reference/debug callers to use the brute-force path.
            if(bound.w>0){
                vec3 delta=bound.xyz-p;float projection=dot(delta,dir);
                float perpendicular=max(0.0,dot(delta,delta)-projection*projection);
                if(projection+bound.w<=0 || perpendicular>bound.w*bound.w){k+=7;continue;}
            }
        }
#endif
        vec3 offset=uBubbles[k].xyz-p;
        float along=dot(offset,dir), radiusBubble=uBubbles[k].w;
        float distance2=max(0.0,dot(offset,offset)-along*along);
#ifdef BUBBLE_DEFORMATION
        // Analytic spheroid intersection: axial radius r*s, perpendicular
        // radii r/sqrt(s), so abc=r^3. No new ray march or texture samples.
        // Zero shape keeps the original spherical calculation exactly.
        vec4 shape=BubbleShape[k];
        if(shape.w>0.0 && abs(shape.w-1.0)>.002){
            float axial=1.0/(shape.w*shape.w),perpendicular=shape.w;
            float qa=dot(offset,shape.xyz),da=dot(dir,shape.xyz);
            float A=perpendicular+(axial-perpendicular)*da*da;
            float B=perpendicular*dot(offset,dir)+(axial-perpendicular)*qa*da;
            float C=perpendicular*dot(offset,offset)+(axial-perpendicular)*qa*qa;
            along=B/A;distance2=max(0.0,C-B*along);
        }
#endif
        if(along>0.0 && distance2<radiusBubble*radiusBubble && uTrace.y>0.0) {
            float rim=smoothstep(0.1,1.0,distance2/(radiusBubble*radiusBubble));
            float edge=1.0-smoothstep(0.85,1.0,distance2/(radiusBubble*radiusBubble));
            vec3 visibility=exp(-uAbsorption*min(along,lengthLiquid)*0.65);
            bubbleLight+=vec3(0.65,0.77,0.8)*(0.14+rim*0.9)*edge*visibility;
        }
    }
    liquid+=bubbleLight;
#ifndef NUKA_SCENE_TRANSMISSION
    liquid=mix(liquid,liquid+uFoamColor*0.45,topLight*uFoam);
#endif
    // The old unconditional pale addition made the entire surface look like foam.
    // Keep the absorbed liquid color, with only a restrained angle-dependent reflection.
    float surfaceReflection=0.0; // No repeated bright bands from ray samples near the surface.
    liquid=mix(liquid,studio(reflect(dir,uPlane.xyz)),topLight*surfaceReflection*(1.0-uFoam));
    float thickness=uOptics.z/max(abs(dot(N,-incoming)),0.15);
    liquid*=exp(-uGlassAbsorption*thickness);
    transmission=attenuation*exp(-uGlassAbsorption*thickness)*(1.0-f)*(1.0-topLight*surfaceReflection*(1.0-uFoam));
    return mix(liquid,reflected,f);
}
vec3 shadeBottle(vec3 p,vec3 normal,vec3 eye) {
    vec3 transmission;
    float liquidPresence;
    vec4 surfaceHit;
    float liquidPath;
    return shadeBottleParts(p,normal,eye,transmission,liquidPresence,surfaceHit,liquidPath);
}

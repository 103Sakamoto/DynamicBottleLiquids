#pragma once
#include <array>
#include <vector>
#include <random>
#include <cstddef>

namespace nuka {
struct Vec { float x{},y{},z{}; Vec operator+(Vec b)const{return{x+b.x,y+b.y,z+b.z};} Vec operator-(Vec b)const{return{x-b.x,y-b.y,z-b.z};} Vec operator*(float a)const{return{x*a,y*a,z*a};} };
float Dot(Vec a,Vec b);
float Length(Vec a);
Vec Normalize(Vec a);
struct alignas(16) V4 { float x{},y{},z{},w{}; };
// Shared prefix with Bottle.hlsl; FO4Bottle.hlsl additionally consumes meshToWorld.
struct alignas(16) Constants {
    float matrix[16]{};
    V4 eye{},plane{},rings[16]{};
    V4 wave{},optics{},trace{},absorption{},scatter{},emission{},glassAbsorption{},foamColor{};
    V4 bubbles[192]{};
    int bubbleCount{}; float padding[3]{};
    int ringCount{};float capZ{},foam{};int debug{};
    // Native reflection basis, independent of the canonical liquid coordinate system.
    V4 meshToWorld[3]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    V4 surfaceDirection[4]{},surfaceLight[4]{};
    float sunMatrices[4][16]{};
    V4 sunBounds[4]{},sunInfo{},sunSlices{},sunVolume{},worldOrigin{},materialFlags{};
    V4 bubbleGroups[24]{}; // conservative bounds for eight nearby spheres
    V4 wetFilm[64]{}; // 16 height rows x 16 angular samples, packed four per vector
    V4 wetInfo{}; // exposed strength, bottom, height-to-row scale, reserved
    V4 wetTuning{0,.15f,.8f,3.5f};
    V4 waveDetail{0,1,1,0}; // secondary amplitude, signed gains, surface-noise envelope
    // Authored Effect cubemap strength, lighting influence, LIGHTING variant, reserved.
    // Independent of the liquid/detail distance weights.
    V4 surfaceEnvironment{1,1,0,0};
    V4 simulationToMesh[3]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    // Original BGSM response for converted glass. Effect materials stay live.
    V4 surfaceSpecular{1,1,1,1}; // colour, multiplier
    V4 surfaceResponse{1,5,0,1}; // smoothness, Fresnel power, BGSM family, original env scale
    V4 surfaceAlpha{1,0,0,0}; // live base alpha, FALLOFF, reserved, reserved
    V4 surfaceFalloff{0,1,1,1}; // live native falloff constants (already in cosine space)
    // Appended only: all native surface/transform constant offsets stay unchanged.
    V4 bubbleShape[192]{}; // local unit axis + volume-preserving axial stretch; zero = sphere
};
static_assert(sizeof(Constants)==8816);
static_assert(offsetof(Constants,bubbleShape)==5744);
struct Settings {
    Constants base{};
    float fill{},frequency{},damping{},gain{},maxTilt{},waveAmplitude{},waveFrequency{},foamStrength{};
    float intensity{},baseline{},movement{},radiusMin{},radiusMax{},rise{},settle{};
    std::vector<V4> samples;
    std::vector<float> sampleMeniscus;
    // sin/cos of each sample's fixed diagonal wave phase; built once per profile.
    std::vector<V4> sampleWaveBasis;
    float sampleWaveFrequency{};
    std::vector<V4> wetSamples,wetPhases;
    std::array<V4,16> wetRings{};int wetRingCount=0;float wetFrequency=0;
    float shakingBubbles{}; // 0..1, independent of resting carbonation
};
struct Particle {Vec position;float radius,speed,age;float baseRadius{};Vec velocity{},shapeAxis{0,0,1};float stretch=1,stretchVelocity{};};
struct ShakingParticle {Vec position,velocity;float radius{},baseRadius{},age{},phase{};Vec shapeAxis{0,0,1};float stretch=1,stretchVelocity{};};
struct Motion {
    double last{};
    float time{},activity{},credit{},foamActivity{},waveEnergy{},rippleEnergy{},surfaceNoise{};
    Vec waveMix{1,1,0};
    Vec lastPosition{},lastUp{0,0,1},velocity{},slosh{},sloshVelocity{};
    std::array<float,256> wetFilm{};
    std::vector<Particle> bubbles;
    std::array<Vec,4> bubbleSources{};
    std::array<bool,4> bubbleSourceValid{};
    std::vector<std::pair<float,float>> volumeScratch;
    Vec solvedNormal{};
    float solvedHeight{},solvedFill{},solvedAmplitude=-1;
    std::mt19937 random{12345};
    float shakeActivity{},shakeCredit{},shakeAngularSpeed{};
    std::vector<ShakingParticle> shaking;
    std::mt19937 shakeRandom{54321}; // Never advances the carbonation RNG.
    Vec shakeOmega{},previousShakeAcceleration{},previousShakeOmega{};
    float previousShakeDrive{},shakeBurstCooldown{};
    Vec emissionOmega{},previousEmissionOmega{};
    bool emissionValid=true;
    float rotationMemory{},filteredAngularAcceleration{},flowEnergy{};
    // Emission-only measurements. Physical slosh and render transforms retain
    // the original world acceleration/basis. Translation is carry-relative.
    float emissionVerticalSpeed{},emissionVerticalAcceleration{};
    bool emissionTranslationValid=false;
    float rotationStroke{},verticalStroke{},verticalStrokeSpeed{},previousVerticalSpeed{};
    float bubbleSizeSpeed{},emissionSpeedHold{},bubbleSizeHold{};
    float verticalPeakAge{},triggerSizeSpeed{};
    unsigned triggerSources{},shakingBirths{};
};
inline bool WithinEffectDistance(Vec bottle,Vec camera){auto d=bottle-camera;return Dot(d,d)<=768.f*768.f;}
inline bool WithinDetailDistance(Vec bottle,Vec camera){auto d=bottle-camera;return Dot(d,d)<=256.f*256.f;}
inline void ApplyDetailDistance(Constants& c,bool detail){c.optics.w=detail?0.f:1.f;if(!detail){c.trace.z=0;c.bubbleCount=0;c.scatter.w=0;c.wetInfo={};}}
void MakeSamples(Settings& s);
float MeniscusHeight(const Constants& c,Vec p);
Constants Simulate(const Settings& s,Motion& m,Vec localAcceleration,Vec up,float dt,bool detail=true,bool held=false);
}

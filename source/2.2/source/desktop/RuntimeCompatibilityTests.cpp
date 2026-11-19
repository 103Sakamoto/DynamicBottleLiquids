#include "PCH.h"
#include "RuntimeCompatibility.h"
#include <RE/I/Interface3D.h>
#include <RE/B/BSGraphics.h>
#include <iostream>

static_assert(nuka::AuditedRuntime({1,10,163,0}));
static_assert(nuka::AuditedRuntime({1,10,984,0}));
static_assert(nuka::AuditedRuntime({1,11,240,0}));
static_assert(!nuka::AuditedRuntime({1,10,980,0}));
static_assert(!nuka::AuditedRuntime({1,11,191,0}));
static_assert(!nuka::AuditedRuntime({1,2,72,0}));
static_assert(sizeof(RE::BSRenderPass)==0x58);
static_assert(offsetof(RE::BSRenderPass,geometry)==0x18);
static_assert(offsetof(RE::BSRenderPass,sceneLights)==0x30);
static_assert(offsetof(RE::BSRenderPass,propertyNext)==0x38);
static_assert(offsetof(RE::BSRenderPass,passEnum)==0x48);
static_assert(offsetof(RE::BSRenderPass,passType)==0x4c);
static_assert(offsetof(RE::BSRenderPass,numLights)==0x50);
static_assert(offsetof(RE::Interface3D::Renderer,offscreen3DEnabled)==0x4d);
static_assert(offsetof(RE::Interface3D::Renderer,screenAttachedElementRoot)==0x80);
static_assert(offsetof(RE::Interface3D::Renderer,offscreenElement)==0x88);
static_assert(offsetof(RE::Interface3D::Renderer,elementRequiringFullVieport)==0xa8);
static_assert(offsetof(RE::BSGraphics::State,frameCount)==0x98);
static_assert(offsetof(RE::BSGraphics::State,cameraState)==0x160);
int main(){std::cout<<"PASS: exact OG/NG/AE runtime allowlist; unknown/VR versions rejected; shared accessed structure offsets locked\n";}

#if defined(REDUCE)
Texture2D<float4> Field:register(t0);
RWByteAddressBuffer Bounds:register(u0);
groupshared uint4 Tile;
[numthreads(16,16,1)]void CS(uint3 id:SV_DispatchThreadID,uint lane:SV_GroupIndex){
 if(lane==0)Tile=0xffffffff;GroupMemoryBarrierWithGroupSync();
 uint w,h;Field.GetDimensions(w,h);
 if(id.x<w&&id.y<h&&Field.Load(int3(id.xy,0)).a!=0){
  InterlockedMin(Tile.x,id.x);InterlockedMin(Tile.y,id.y);InterlockedMin(Tile.z,~id.x);InterlockedMin(Tile.w,~id.y);
 }
 GroupMemoryBarrierWithGroupSync();
 if(lane==0&&Tile.x!=0xffffffff){uint unused;Bounds.InterlockedMin(0,Tile.x,unused);Bounds.InterlockedMin(4,Tile.y,unused);Bounds.InterlockedMin(8,Tile.z,unused);Bounds.InterlockedMin(12,Tile.w,unused);}
}
#elif defined(FINISH)
ByteAddressBuffer Bounds:register(t0);
Texture2D<float4> Scene:register(t1);
RWByteAddressBuffer Args:register(u0);
[numthreads(1,1,1)]void CS(){
 uint4 b=Bounds.Load4(0);uint w,h;Scene.GetDimensions(w,h);
 if(b.x==0xffffffff){Args.Store3(0,uint3(0,0,1));Args.Store4(12,0);return;}
 b.zw=~b.zw;
 // Conservative pixel margin retained for regional composition.
 int2 lo=max(int2(0,0),int2(b.xy)-3);
 int2 hi=min(int2(w,h),int2(b.zw)+4);
 // Malformed bounds must never shrink the safe region.
 if(any(b.xy>b.zw)||b.z>=w||b.w>=h){lo=0;hi=int2(w,h);}
 Args.Store3(0,uint3((uint2(hi-lo)+7)/8,1));Args.Store4(12,uint4(lo,hi));
}
#else
Texture2D<float4> Composed:register(t0);
ByteAddressBuffer Args:register(t6);
RWTexture2D<float4> Scene:register(u0);
[numthreads(8,8,1)]void CS(uint3 id:SV_DispatchThreadID){uint4 r=Args.Load4(12);uint2 p=r.xy+id.xy;if(all(p<r.zw))Scene[p]=Composed.Load(int3(p,0));}
#endif

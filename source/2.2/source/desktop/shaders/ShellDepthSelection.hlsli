// Raw D32 selection avoids the precision loss of adding 1 and packing depth
// into rotating ranges. Compare the identical rasterized depth directly:
// a tolerance can admit farther shells and flicker with projection jitter.
cbuffer ShellSelectionConstants : register(b9) { float ShellSelectionEnabled; float3 ShellDepthPadding; };
Texture2D<float> ShellSelectionDepth : register(t19);
float EncodeShellDepth(float depth) { return depth; }
void SelectNearestShell(float4 position) {
    if(ShellSelectionEnabled>0) {
        float nearest=ShellSelectionDepth.Load(int3(int2(position.xy),0));
        if(position.z>nearest)discard;
    }
}

// Captured FO4 refraction consumer: delta = (-1,+1)*0.25*B*(RG-0.5).
// This encodes the initial displacement, before native screen-edge/coverage policy.
float4 encodeNativeDistortion(float2 delta) {
    if (any((asuint(delta) & 0x7f800000u) == 0x7f800000u)) delta=0;
    // Keep B=1: optical strength belongs in delta, not a second multiplier.
    // Alpha marks valid coverage, including an opaque/no-displacement pixel.
    return float4(float2(.5,.5)+float2(-4,4)*delta,1,1);
}
float2 decodeNativeDistortion(float4 field) {
    return float2(-1,1)*.25*field.z*(field.xy-.5);
}
float3 composeOpticalAppearance(float3 localLight,float3 transmission,float3 background) {
    return localLight+transmission*background;
}

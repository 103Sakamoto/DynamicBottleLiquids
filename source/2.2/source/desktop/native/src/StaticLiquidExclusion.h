#pragma once
#include <string_view>
namespace nuka {
// Deliberately narrow trial: these are identity carriers, not drawable liquid.
// Other mesh families, labels, caps and fins must never match this exclusion.
inline bool IsOppressorStaticLiquid(std::string_view name,unsigned vertices,unsigned indices) {
    return (name=="Inside_Liquid_Low:0"&&vertices==1763&&indices==8868)||
           (name=="Inside_Liquid_Top_Low:0"&&vertices==50&&indices==102);
}
inline bool IsOppressorLiquidName(std::string_view name) {
    return name=="Inside_Liquid_Low:0"||name=="Inside_Liquid_Top_Low:0";
}
inline bool IsOppressorLiquidOwner(std::string_view name,unsigned vertices,unsigned indices,
                                  std::string_view source,unsigned sourceVertices,unsigned sourceIndices) {
    return name=="DynamicBottleLiquids:op_nukacola"&&vertices==2932&&indices==15132&&
           source=="Inside_Liquid_Low:0"&&sourceVertices==1763&&sourceIndices==8868;
}
}

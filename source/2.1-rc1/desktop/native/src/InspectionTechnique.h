#pragma once
#include <cstdint>
namespace nuka {
constexpr std::uint32_t inspectionTechniqueBit=1u<<30;
constexpr bool InspectionColourTechnique(std::uint32_t technique){
    constexpr auto required=(1u<<4)|(1u<<19)|(1u<<21);
    constexpr auto excluded=(1u<<1)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<18)|(1u<<29);
    return (technique&inspectionTechniqueBit)&&(technique&required)&&!(technique&excluded);
}
// Validation is deferred until the engine reports failure, so supported native
// techniques keep their original single draw and do not perform profile lookups.
template<class Pass,class Original,class Validate,class Report>
bool RetryInspectionTechnique(Pass* pass,std::uint32_t technique,bool setup,
    Original original,Validate validate,Report report){
    const bool result=original(pass,technique,setup);
    if(result||!pass||pass->passEnum!=technique||!InspectionColourTechnique(technique)||!validate(pass))return result;
    struct Restore {Pass* pass;std::uint32_t value;~Restore(){pass->passEnum=value;}} restore{pass,pass->passEnum};
    pass->passEnum=technique&~inspectionTechniqueBit;
    const bool retried=original(pass,pass->passEnum,setup);
    report(technique,pass->passEnum,retried);
    return retried;
}
}

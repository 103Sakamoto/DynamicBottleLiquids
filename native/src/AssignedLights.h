#pragma once
namespace nuka {
// This is the engine-provided list length, not the GPU's four-point-light limit.
// Directional/null entries must not consume a local-light budget.
template<class T,class F> void ForEachAssignedLight(T** lights,unsigned count,F visit){
    if(!lights)return;
    for(unsigned i=0;i<count;i++)if(lights[i])visit(lights[i]);
}
}

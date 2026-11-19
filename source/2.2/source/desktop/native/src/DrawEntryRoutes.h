#pragma once
#include <array>
#include <cstddef>
#include <utility>
namespace nuka {
// Per-entry trampolines: another plugin may switch a context between these entries.
template<class Fn, std::size_t Capacity=16> struct DrawEntryRoutes {
    struct Entry { void* target=nullptr; Fn next=nullptr; };
    std::array<Entry,Capacity> entries{};
    std::size_t count=0;
    inline static thread_local unsigned depth=0;
    template<class Work,class... Args> void Invoke(std::size_t slot,Work&& work,Args... args) {
        auto next=entries[slot].next;
        struct Scope {unsigned& depth;Scope(unsigned& d):depth(d){++depth;}~Scope(){--depth;}} scope(depth);
        if(depth>1){next(args...);return;}
        std::forward<Work>(work)(next,args...);
    }
};
}

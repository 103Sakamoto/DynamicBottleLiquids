#pragma once
#include <string_view>
namespace nuka {
// VF clones retain geometry but prefix names for animation isolation. Never rename
// scene objects; only canonicalize a known DBL tag at the lookup boundary.
inline std::string_view BottleShapeName(std::string_view name) {
    auto candidate=name;
    while(candidate.starts_with("VF_"))candidate.remove_prefix(3);
    return candidate.starts_with("DynamicBottleLiquids:")?candidate:name;
}
// Used only for ancillary shapes inside an already identified bottle, or with
// explicit native-shell name/count checks.
inline std::string_view DisplaySourceName(std::string_view name) {
    while(name.starts_with("VF_"))name.remove_prefix(3);
    return name;
}
}

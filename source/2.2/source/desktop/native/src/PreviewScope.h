#pragma once
#include <array>
namespace nuka {
template<class Node> struct PreviewRoots {
 std::array<const Node*,3> roots{};
 bool Contains(const Node* node)const {
  if(!roots[0]&&!roots[1]&&!roots[2])return false;
  for(unsigned i=0;node&&i<128;++i,node=node->parent)
   for(auto* root:roots)if(root&&node==root)return true;
  return false;
 }
};
}

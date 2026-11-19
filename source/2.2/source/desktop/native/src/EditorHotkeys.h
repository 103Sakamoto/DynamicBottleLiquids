#pragma once
#include <Windows.h>
#include <string>
#include <stdexcept>
namespace nuka {
struct EditorHotkey {
 int key=VK_F7;bool ctrl=true,shift=false,alt=false;
 bool operator==(const EditorHotkey&)const=default;
 std::string Text()const{return std::string(ctrl?"Ctrl+":"")+(shift?"Shift+":"")+(alt?"Alt+":"")+"F"+std::to_string(key-VK_F1+1);}
 static EditorHotkey Parse(std::string value){
  EditorHotkey result;result.ctrl=false;size_t at=0;
  for(auto end=value.find('+',at);end!=std::string::npos;end=value.find('+',at)){auto token=value.substr(at,end-at);if(token=="Ctrl"&&!result.ctrl)result.ctrl=true;else if(token=="Shift"&&!result.shift)result.shift=true;else if(token=="Alt"&&!result.alt)result.alt=true;else throw std::runtime_error("Use Ctrl, Shift or Alt with F1-F12");at=end+1;}
  auto token=value.substr(at);if(token.size()<2||token[0]!='F')throw std::runtime_error("Choose F1-F12");
  size_t used=0;int f=std::stoi(token.substr(1),&used);if(used!=token.size()-1||f<1||f>12)throw std::runtime_error("Choose F1-F12");result.key=VK_F1+f-1;if(result.alt&&result.key==VK_F4)throw std::runtime_error("Alt+F4 is reserved for closing the game");return result;
 }
 bool Matches(bool focus,bool keyDown,bool control,bool sh,bool a)const{return focus&&keyDown&&ctrl==control&&shift==sh&&alt==a;}
};
struct HotkeyEdge {bool previous=false;bool Update(bool down){bool pressed=down&&!previous;previous=down;return pressed;}};
}

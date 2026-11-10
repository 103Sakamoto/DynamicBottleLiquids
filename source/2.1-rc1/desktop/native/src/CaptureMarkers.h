#pragma once
#include <d3d11_1.h>

namespace nuka {
// Annotation events only: no render state, draw or query changes.
// GetStatus avoids submitting markers when no graphics capture tool is attached.
class CaptureScope {
    ID3DUserDefinedAnnotation* annotation=nullptr;
public:
    CaptureScope(ID3DUserDefinedAnnotation* value,const wchar_t* name) {
        if(value && value->GetStatus()) {annotation=value;annotation->BeginEvent(name);}
    }
    CaptureScope(const CaptureScope&)=delete;
    CaptureScope& operator=(const CaptureScope&)=delete;
    void End() {if(annotation){annotation->EndEvent();annotation=nullptr;}}
    ~CaptureScope(){End();}
};
}

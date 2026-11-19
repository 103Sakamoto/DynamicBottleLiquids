"""Verify F4SE metadata and the release banner in the actual installer DLLs."""
from pathlib import Path
from struct import unpack_from
from zipfile import ZipFile

def export_offset(data,name):
    u32=lambda off:unpack_from('<I',data,off)[0]
    u16=lambda off:unpack_from('<H',data,off)[0]
    pe=u32(0x3c)
    optional=pe+24
    assert data[pe:pe+4]==b'PE\0\0' and u16(optional)==0x20b
    sections=optional+u16(pe+20)
    def offset(rva):
        for i in range(u16(pe+6)):
            virtual_size,va,size,raw=unpack_from('<IIII',data,sections+40*i+8)
            if va<=rva<va+max(size,virtual_size):
                return raw+rva-va
        raise ValueError(rva)
    exports=offset(u32(optional+112))
    functions,names,ordinals=map(offset,unpack_from('<III',data,exports+28))
    for index in range(u32(exports+24)):
        addr=offset(u32(names+4*index))
        if data[addr:data.index(b'\0',addr)]==name:
            return offset(u32(functions+4*u16(ordinals+2*index)))
    raise ValueError(name)

root=Path(__file__).resolve().parent
with ZipFile(root/'DynamicBottleLiquids-2.2-OG-NG-AE-FOMOD.zip') as archive:
    for ui in ('fusion','mf','dmui','none'):
        data=archive.read(f'components/ui-{ui}/F4SE/Plugins/DynamicBottleLiquids.dll')
        pos=export_offset(data,b'F4SEPlugin_Version')
        version=unpack_from('<I',data,pos+4)[0]
        assert version==0x02020000,(ui,hex(version))
        for phrase in (b'DynamicBottleLiquids 2.2 initializing.',b'DynamicBottleLiquids 2.2 loaded.'):
            assert phrase in data,(ui,phrase)
        for phrase in (b'1.3.2 loaded',b'prototype test',b'mirror compatibility test',b'native-only test',b'test 7 droplets',b'profiling locked',b'separate from release'):
            assert phrase not in data.lower(),(ui,phrase)
        print(ui,'F4SE 2.2.0.0, startup and success banners verified')
    assert archive.read('components/desktop/F4SE/Plugins/DynamicBottleLiquids.ini').splitlines()==[b'[Logging]',b'Mode=Basic']

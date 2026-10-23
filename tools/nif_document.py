"""Shared NIF block helpers extracted unchanged from the existing patch builders."""
import struct
from nif_mesh import Reader


def parse(raw):
    r=Reader(raw);r.take(raw.index(b'\n')+1);r.take(9);count_offset=r.pos;n=r.get('I');r.get('I')
    for _ in range(4):r.take(r.get('B'))
    types=[r.string() for _ in range(r.get('H'))];prefix=raw[:r.pos]
    ids=[r.get('H') for _ in range(n)];sizes=[r.get('I') for _ in range(n)]
    ns,_=r.get('II');strings=[r.string() for _ in range(ns)]
    groups=r.get('I');assert groups==0
    chunks=[r.take(size) for size in sizes];roots=[r.get('i') for _ in range(r.get('I'))];assert r.pos==len(raw)
    return count_offset,prefix,types,ids,strings,chunks,roots


def object_refs(data,start=0,av=False):
    r=Reader(data);r.pos=start;r.get('I');n=r.get('I');refs=[]
    for _ in range(n):refs.append(r.pos);r.get('i')
    refs.append(r.pos);r.get('i')
    if av:r.take(4+12+36+4);refs.append(r.pos);r.get('i')
    return r.pos,refs


def save(parts,path):
 co,pre,types,ids,strings,chunks,roots=parts
 head=bytearray(pre);struct.pack_into('<I',head,co,len(chunks))
 head+=struct.pack('<'+'H'*len(ids),*ids)+struct.pack('<'+'I'*len(chunks),*[len(c) for c in chunks])
 encoded=[s.encode() for s in strings];head+=struct.pack('<II',len(strings),max(map(len,encoded)))
 head+=b''.join(struct.pack('<I',len(s))+s for s in encoded)+struct.pack('<I',0)
 path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(head+b''.join(chunks)+struct.pack('<I',len(roots))+struct.pack('<'+'i'*len(roots),*roots))


def intern(strings,s):
 if s not in strings:strings.append(s)
 return strings.index(s)


def hide(b):
 b=bytearray(b);end,refs=object_refs(b,av=True);offset=end-60;flags=struct.unpack_from('<I',b,offset)[0];struct.pack_into('<I',b,offset,flags|1);return bytes(b)


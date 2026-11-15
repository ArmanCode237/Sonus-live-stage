"""Synthetic legal SF2 (one instrument, one preset), no licensed samples."""
from pathlib import Path
import math, struct, sys
out=Path(sys.argv[1])
def chunk(id,data):return id.encode()+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')
def lst(name,items):return chunk('LIST',name.encode()+b''.join(items))
def cname(name,size):return name.encode()[:size].ljust(size,b'\0')
def gen(op,value):return struct.pack('<HH',op,value&0xffff)
phdr=cname('Smoke Bank',20)+struct.pack('<HHHIII',0,0,0,0,0,0)+cname('EOP',20)+struct.pack('<HHHIII',0,0,1,0,0,0)
pbag=struct.pack('<HHHH',0,0,1,0)
pgen=gen(41,0)
inst=cname('Smoke Instrument',20)+struct.pack('<H',0)+cname('EOI',20)+struct.pack('<H',1)
ibag=struct.pack('<HHHH',0,0,2,0)
igen=gen(53,0)+gen(54,1)
wave=[int(16000*math.sin(2*math.pi*i/128)) for i in range(512)]+[0]*46
smpl=struct.pack('<'+'h'*len(wave),*wave)
shdr=cname('Smoke C4',20)+struct.pack('<IIIIIBbHH',0,512,128,480,44100,60,0,0,1)+cname('EOS',20)+struct.pack('<IIIIIBbHH',512,512,512,512,44100,60,0,0,1)
data=b'RIFF'+struct.pack('<I',0)+b'sfbk'+lst('sdta',[chunk('smpl',smpl)])+lst('pdta',[chunk('phdr',phdr),chunk('pbag',pbag),chunk('pmod',bytes(10)),chunk('pgen',pgen),chunk('inst',inst),chunk('ibag',ibag),chunk('imod',bytes(10)),chunk('igen',igen),chunk('shdr',shdr)])
data=data[:4]+struct.pack('<I',len(data)-8)+data[8:]
out.write_bytes(data)
print('Generated',out,len(data))

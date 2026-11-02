#!/usr/bin/env python3
"""Deterministically encode a miniRV-only self-checking board smoke program."""
from pathlib import Path
import json
def i(imm,rs,funct,rd,op):return ((imm&4095)<<20)|(rs<<15)|(funct<<12)|(rd<<7)|op
def s(imm,rs,src,funct):return ((imm&4064)<<20)|(src<<20)|(rs<<15)|(funct<<12)|((imm&31)<<7)|0x23
def b(imm,rs,rt):return (((imm>>12)&1)<<31)|(((imm>>5)&63)<<25)|(rt<<20)|(rs<<15)|(1<<12)|(((imm>>1)&15)<<8)|(((imm>>11)&1)<<7)|0x63
p=[]; branches=[]
def emit(word):p.append(word)
def check(rs,rt):branches.append((len(p),rs,rt));emit(0)
emit((0x80000<<12)|(1<<7)|0x37)
emit((0x12345<<12)|(2<<7)|0x37);emit(i(0x678,2,0,2,0x13))
emit(s(0,1,2,2));emit(i(0,1,2,3,3));check(2,3)
for lane,value in enumerate([0x11,0x22,0x33,0x44]):
    emit(i(value,0,0,2,0x13));emit(s(lane,1,2,0))
    emit(i(lane,1,4,3,3));check(2,3)
emit(i(0,1,2,3,3));emit((0x44332<<12)|(4<<7)|0x37);emit(i(0x211,4,0,4,0x13));check(3,4)
emit((0x10000<<12)|(6<<7)|0x37);emit(i(0xA5,0,0,2,0x13));emit(s(4,6,2,2))
emit(0x00100073)
fail=len(p);emit(0)  # illegal instruction: failure must not masquerade as EBREAK.
for at,rs,rt in branches:p[at]=b((fail-at)*4,rs,rt)
out=Path(__file__).parent
(out/'smoke.hex').write_text(''.join(f'{w:08x}\n' for w in p),encoding='ascii')
(out/'smoke.json').write_text(json.dumps({'instructions':len(p),'success_commits':fail,'failure_pc':fail*4,'signature':165,'words':p},indent=2)+'\n')
print(f'Generated {len(p)} words; success {fail} commits, GPIO=0xA5')

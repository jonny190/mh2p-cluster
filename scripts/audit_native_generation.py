"""Read-only audit against saved P2873 ELF, with our existing ELF reader.
No firmware execution. CC BY-NC-SA 4.0; see LICENSE.md.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parents[1]
HASH="fb1ffd3196aa99bc254e57c6f432ea53f5ac3070a63b69e6b41a5ff40de8e50b"


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--output",type=Path,default=ROOT/"build-local/native-generation-audit.json")
    args=p.parse_args()
    spec=importlib.util.spec_from_file_location("saved_elf_reader",WORKSPACE/"research/commands/read_hook_placement.py")
    reader=importlib.util.module_from_spec(spec); spec.loader.exec_module(reader)
    elf=reader.Elf("mnt/app/eso/lib/libautoreceiver.so",HASH)
    source=(ROOT/"src/gal_cluster.c").read_text(encoding="utf-8")+"\n"+(ROOT/"src/cluster_receiver_adapter.h").read_text(encoding="utf-8")
    names=sorted(set(re.findall(r"^(?:void|int|int32_t)\s+(_ZN\w+)\s*\(",source,re.MULTILINE)))
    checked=[]
    for name in names:
        symbol=elf.symbols.get(name)
        if not symbol or not symbol["value"] or not symbol["defined"]:
            raise ValueError("Target hook symbol absent/undefined: "+name)
        checked.append({"name":name,"thumb_pointer":symbol["value"],"entry":symbol["value"]&~1})
    fields={}
    for name,expected in (("_ZN5Start21kSessionIdFieldNumberE",1),("_ZN5Start30kConfigurationIndexFieldNumberE",2),
        ("_ZN3Ack21kSessionIdFieldNumberE",1),("_ZN3Ack15kAckFieldNumberE",2)):
        value=struct.unpack("<I",elf.raw(elf.symbols[name]["value"],4))[0]
        if value!=expected: raise ValueError("Unexpected Start field constant: "+name)
        fields[name]=value
    result={"stock_receiver_sha256":HASH,"exports_checked":checked,"start_field_numbers":fields,
        "limits":"Existence only; no QNX loader-hit, complete ABI, mutex ordering, NvMedia or Screen proof.",
        "contracts":{"router_init":"int32, forwards1/0; target0xf0854", "router_shutdown":"void; target0xf0878",
            "receiver_prepare":"void; target0xeb350", "local_close":"void; target0xf0ce8",
            "endpoint_close":"previous open gate; target0xf2948", "service_table":"0x100 + service*4",
            "manager_pointer":"0x4fc", "iobuffer":"wrapper+4 -> bytes+0, capacity+4, offset+8, end+12",
            "send_copy":"ChannelManager::messageToFrames0xe8268 malloc+memcpy before return; free our injected buffers only"}}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(str(len(checked))+" hook definitions exist in exact saved receiver; metadata written to "+str(args.output))


if __name__=="__main__": main()

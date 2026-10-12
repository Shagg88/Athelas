import sys, pefile, datetime, math, collections
pe = pefile.PE(sys.argv[1])
fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
print("Timestamp:", datetime.datetime.utcfromtimestamp(fh.TimeDateStamp))
print("ImageBase: %#x  EntryPoint RVA: %#x  Linker %d.%d" % (oh.ImageBase, oh.AddressOfEntryPoint, oh.MajorLinkerVersion, oh.MinorLinkerVersion))
def ent(b):
    if not b: return 0
    c = collections.Counter(b); n=len(b)
    return -sum(v/n*math.log2(v/n) for v in c.values())
for s in pe.sections:
    d = s.get_data()
    print("%-10s VA=%#09x VSize=%#09x RawOff=%#09x RawSize=%#09x ent=%.2f chars=%#x" % (s.Name.rstrip(b'\0').decode(errors='replace'), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData, ent(d), s.Characteristics))
ep = oh.AddressOfEntryPoint
for s in pe.sections:
    if s.contains_rva(ep): print("Entry point in section:", s.Name.rstrip(b'\0'))
print("Overlay:", pe.get_overlay_data_start_offset())
if hasattr(pe,'DIRECTORY_ENTRY_IMPORT'):
    for e in pe.DIRECTORY_ENTRY_IMPORT:
        names = [i.name.decode() if i.name else "ord%d"%i.ordinal for i in e.imports]
        print(e.dll.decode(), len(names), ", ".join(names[:60]))
if hasattr(pe,'VS_FIXEDFILEINFO'):
    for fi in pe.FileInfo or []:
        for x in fi:
            if hasattr(x,'StringTable'):
                for st in x.StringTable:
                    for k,v in st.entries.items(): print(k.decode(), "=", v.decode())

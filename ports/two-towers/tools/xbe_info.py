#!/usr/bin/env python3
"""Dump the structure of an Xbox XBE: header, certificate, sections,
library versions, TLS and kernel imports. Usage: xbe_info.py Default.xbe [--json out.json]"""
import struct, sys, json

ENTRY_XOR = {"retail": 0xA8FC57AB, "debug": 0x94859D4B}
THUNK_XOR = {"retail": 0x5B6D40B6, "debug": 0xEFB1F152}

class XBE:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        assert d[:4] == b"XBEH", "not an XBE"
        u = lambda o: struct.unpack_from("<I", d, o)[0]
        self.base = u(0x104)
        h = self.h = {k: u(o) for k, o in [
            ("size_headers", 0x108), ("size_image", 0x10C), ("size_image_header", 0x110),
            ("timedate", 0x114), ("cert_addr", 0x118), ("num_sections", 0x11C),
            ("section_headers_addr", 0x120), ("init_flags", 0x124), ("entry_enc", 0x128),
            ("tls_addr", 0x12C), ("pe_stack_commit", 0x130), ("pe_heap_reserve", 0x134),
            ("pe_heap_commit", 0x138), ("pe_base", 0x13C), ("pe_size_image", 0x140),
            ("pe_checksum", 0x144), ("pe_timedate", 0x148), ("debug_path_addr", 0x14C),
            ("debug_file_addr", 0x150), ("debug_ufile_addr", 0x154), ("thunk_enc", 0x158),
            ("nonkernel_import_addr", 0x15C), ("num_libs", 0x160), ("libs_addr", 0x164),
            ("kernel_lib_addr", 0x168), ("xapi_lib_addr", 0x16C), ("logo_addr", 0x170),
            ("logo_size", 0x174)]}
        # Decide retail vs debug by which XOR gives an entry inside the image
        for kind in ("retail", "debug"):
            e = h["entry_enc"] ^ ENTRY_XOR[kind]
            if self.base <= e < self.base + h["size_image"]:
                self.kind = kind; self.entry = e
                self.thunk = h["thunk_enc"] ^ THUNK_XOR[kind]
                break
        self._sections()

    def u32(self, off): return struct.unpack_from("<I", self.d, off)[0]
    def hdr_off(self, va): return va - self.base          # header area maps 1:1
    def cstr(self, va, maxlen=256):
        o = self.hdr_off(va); s = self.d[o:o + maxlen]
        return s.split(b"\0")[0].decode("latin1")

    def _sections(self):
        self.sections = []
        o = self.hdr_off(self.h["section_headers_addr"])
        for i in range(self.h["num_sections"]):
            f, va, vs, ra, rs, na, nrc, hp, tp = struct.unpack_from("<9I", self.d, o + i * 56)
            self.sections.append(dict(name=self.cstr(na), flags=f, va=va, vsize=vs,
                                      raw=ra, rsize=rs,
                                      writable=bool(f & 1), preload=bool(f & 2),
                                      executable=bool(f & 4), inserted=bool(f & 8)))

    def va2off(self, va):
        for s in self.sections:
            if s["va"] <= va < s["va"] + s["rsize"]:
                return s["raw"] + va - s["va"]
        if va < self.base + self.h["size_headers"]:
            return va - self.base
        return None

    def section_of(self, va):
        for s in self.sections:
            if s["va"] <= va < s["va"] + max(s["vsize"], s["rsize"]):
                return s["name"]
        return None

    def cert(self):
        o = self.hdr_off(self.h["cert_addr"])
        c = dict(size=self.u32(o), timedate=self.u32(o + 4), title_id=self.u32(o + 8),
                 title_name=self.d[o + 12:o + 12 + 80].decode("utf-16le").split("\0")[0],
                 allowed_media=self.u32(o + 0x9C), game_region=self.u32(o + 0xA0),
                 game_ratings=self.u32(o + 0xA4), disk_number=self.u32(o + 0xA8),
                 version=self.u32(o + 0xAC))
        return c

    def libs(self):
        out = []; o = self.hdr_off(self.h["libs_addr"])
        for i in range(self.h["num_libs"]):
            name = self.d[o:o + 8].split(b"\0")[0].decode()
            ma, mi, b, fl = struct.unpack_from("<4H", self.d, o + 8)
            out.append(dict(name=name, version=f"{ma}.{mi}.{b}", flags=fl,
                            qfe=fl & 0x1FFF, approved=(fl >> 13) & 3, debug=bool(fl & 0x8000)))
            o += 16
        return out

    def kernel_imports(self):
        o = self.va2off(self.thunk); out = []
        while True:
            v = self.u32(o)
            if v == 0: break
            out.append((self.thunk + 4 * len(out), v & 0x7FFFFFFF if v & 0x80000000 else v))
            o += 4
        return out

    def tls(self):
        if not self.h["tls_addr"]: return None
        o = self.va2off(self.h["tls_addr"])
        s, e, idx, cb, zf, ch = struct.unpack_from("<6I", self.d, o)
        return dict(data_start=s, data_end=e, index_addr=idx, callbacks=cb, zero_fill=zf, characteristics=ch)

if __name__ == "__main__":
    x = XBE(sys.argv[1])
    print(f"kind={x.kind} base={x.base:#x} entry={x.entry:#x} thunk={x.thunk:#x}")
    for k, v in x.h.items(): print(f"  {k:24s} {v:#x}")
    print("debug path:", x.cstr(x.h["debug_path_addr"]))
    c = x.cert(); print("cert:", {k: (hex(v) if isinstance(v, int) else v) for k, v in c.items()})
    print("tls:", x.tls())
    print("libraries:")
    for l in x.libs(): print("  ", l)
    print("sections:")
    for s in x.sections:
        fl = "".join(c for c, b in zip("WPXI", (s["writable"], s["preload"], s["executable"], s["inserted"])) if b)
        print(f"  {s['name']:10s} va={s['va']:#010x} vsize={s['vsize']:#9x} raw={s['raw']:#9x} rsize={s['rsize']:#9x} {fl}")
    ki = x.kernel_imports()
    print("kernel imports:", len(ki))
    if "--json" in sys.argv:
        json.dump(dict(kind=x.kind, base=x.base, entry=x.entry, thunk=x.thunk, header=x.h,
                       cert=c, tls=x.tls(), libs=x.libs(), sections=x.sections,
                       kernel_imports=[o for _, o in ki]),
                  open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)

musl libm (MIT licence, COPYRIGHT here): the double precision functions online co-op's lockstep mode uses
(kernel/lockstep.h; cpu/det_math.cpp compiles them), so that two PCs compute the same bits whatever their CPU or
Windows version. Microsoft's ucrtbase.dll (std::sin and the like on Windows) differs between Windows versions and picks
FMA or plain code paths per processor.

From the musl mirror github.com/kraj/musl, commit in VERSION.txt: src/math/*.c and *_data.h as they are, except one
change to the *_data.h files: their "#include <features.h>" (a musl header) is removed. libm.h here is Athelas's own,
a stand-in for musl's src/internal/libm.h with what these files use, made to be included inside a C++ namespace.

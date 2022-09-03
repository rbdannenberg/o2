current:
	echo make pd_linux, pd_linux32, pd_nt, pd_darwin

clean: ; rm -f *.pd_linux *.o *.pd_darwin *.dylib

# ----------------------- Windows-----------------------
# note; you will certainly have to edit the definition of VC to agree with
# whatever you've got installed on your machine:

VC="D:\Program Files\Microsoft Visual Studio\Vc98"

pd_nt: o2ensemble.dll

.SUFFIXES: .obj .dll

PDNTCFLAGS = /W3 /WX /DNT /DPD /nologo

PDNTINCLUDE = /I. /I\tcl\include /I..\..\src /I$(VC)\include

PDNTLDIR = $(VC)\lib
PDNTLIB = $(PDNTLDIR)\libc.lib \
	$(PDNTLDIR)\oldnames.lib \
	$(PDNTLDIR)\kernel32.lib \
	..\..\bin\pd.lib 

.c.dll:
	cl $(PDNTCFLAGS) $(PDNTINCLUDE) /c $*.c
	link /dll /export:$*_setup $*.obj $(PDNTLIB)

# override explicitly for tilde objects like this:
dspobj~.dll: dspobj~.c; 
	cl $(PDNTCFLAGS) $(PDNTINCLUDE) /c $*.c
	link /dll /export:dspobj_tilde_setup $*.obj $(PDNTLIB)

# ----------------------- LINUX i386 -----------------------

pd_linux: o2ensemble_ia64

pd_linux32: o2ensemble.l_i386

.SUFFIXES: .l_i386 .l_ia64

# LINUXCFLAGS = -DPD -O2 -funroll-loops -fomit-frame-pointer \
#     -Wall -W -Wshadow -Wstrict-prototypes -Werror \
#     -Wno-unused -Wno-parentheses -Wno-switch

LINUXCFLAGS = -DPD -g \
    -Wall -W -Wshadow -Wstrict-prototypes -Werror \
    -Wno-unused -Wno-parentheses -Wno-switch

LINUXINCLUDE =  -I../../pd/pd-0.52-2/src

.c.l_i386:
	cc $(LINUXCFLAGS) $(LINUXINCLUDE) -o $*.o -c $*.c
	ld -shared -o $*.l_i386 $*.o -lc -lm
	strip --strip-unneeded $*.l_i386
	rm $*.o

.c.l_ia64:
	cc $(LINUXCFLAGS) $(LINUXINCLUDE) -fPIC -o $*.o -c $*.c
	ld -shared -o $*.l_ia64 $*.o -lc -lm
	strip --strip-unneeded $*.l_ia64
	rm $*.o

# ----------------------- macOS -----------------------

pd_darwin: o2ensemble.pd_darwin o2receive.pd_darwin o2property.pd_darwin o2send.pd_darwin

o2ensemble.pd_darwin: libo2pd.dylib

o2receive.pd_darwin: libo2pd.dylib

o2send.pd_darwin: libo2pd.dylib

o2property.pd_darwin: libo2pd.dylib

o2send.pd_darwin: libo2pd.dylib

.SUFFIXES: .pd_darwin

DARWINCFLAGS = -DPD -g -Wall -W -Wshadow -Wstrict-prototypes \
    -Wno-unused -Wno-parentheses -Wno-switch -arch x86_64 \
    -I../src -DO2_NO_O2DISCOVERY

.o.pd_darwin:
	cc -bundle -undefined suppress -arch x86_64 \
            -flat_namespace -o $*.pd_darwin $*.o -L. -lo2pd
#            -L/Users/rbd/o2/Debug -lo2_static \
#	rm -f $*.o

.c.o:
	cc $(DARWINCFLAGS) $(LINUXINCLUDE) -g -o $*.o -c $*.c

libo2pd.dylib: o2pd.o
	cc -dynamiclib -undefined suppress -flat_namespace \
            -install_name libo2pd.dylib \
            -arch x86_64 -o libo2pd.dylib o2pd.o \
            -all_load /Users/rbd/o2/Debug/libo2_static.a \
            -framework CoreAudio -lstdc++

#####################################################################################
#
#  This make file is for compiling the 
#  DOS uilities
#
#  Use:
#    clean      - clean environment
#    all        - build all outputs
#
#####################################################################################

#------------------------------------------------------------------------------------
# generate debug information for WD.EXE
# change to 'yes' or 'no'
#------------------------------------------------------------------------------------
DEBUG = no

# remove existing implicit rule (specifically '%.o: %c')
.SUFFIXES:

#------------------------------------------------------------------------------------
# project directories
#------------------------------------------------------------------------------------
FLPIMG = floppyB.img
INCDIR = inc
PRECOMP = ws.pch
IPDIR = /home/eyal/data/projects/ip-stack/ip

VPATH = $(INCDIR)

#------------------------------------------------------------------------------------
# IP stack files include
#------------------------------------------------------------------------------------
include $(IPDIR)/ipStackFiles.mk

COREOBJ=$(STACKCORE:.c=.o)
NETIFOBJ=$(INTERFACESLIPSIO:.c=.o)
NETWORKOBJ=$(NETWORK:.c=.o)
TRANSPORTOBJ=$(TRANSPORT:.c=.o)

#------------------------------------------------------------------------------------
# build utilities
#------------------------------------------------------------------------------------
ASM = wasm
CC = wcc
LIB = wlib
LINK = wlink

#------------------------------------------------------------------------------------
# tool options
#------------------------------------------------------------------------------------
ifeq ($(DEBUG),yes)
CCDBG = -d2
ASMDBG = -d1
LINKDBG = DEBUG LINES 
else
CCDBG = -d0
ASMDBG =
LINKDBG =
endif

#CCOPT = -0 -ml $(CCDBG) -zu -fh=$(PRECOMP) -s -i=/home/eyal/bin/watcom/h -i=$(INCDIR)
CCOPT = -0 -ml $(CCDBG) -zu -zp1 -fpc -i=/home/eyal/bin/watcom/h -i=$(INCDIR)
ASMOPT = -0 -ml $(ASMDBG)

LINKCFG = LIBPATH /home/eyal/bin/watcom/lib286/dos \
          LIBPATH /home/eyal/bin/watcom/lib286     \
          FORMAT DOS                               \
          OPTION ELIMINATE                         \
          $(LINKDBG)

#------------------------------------------------------------------------------------
# some variables for the linker
# file names list
#------------------------------------------------------------------------------------
COM = ,
EMPTY =
SPC = $(EMPTY) $(EMPTY)

#------------------------------------------------------------------------------------
# new make patterns
#------------------------------------------------------------------------------------
%.o: %.c
	$(CC) $< $(CCOPT) -fo=$(notdir $@)

#------------------------------------------------------------------------------------
# build all targets
#------------------------------------------------------------------------------------
all: disktest xmodem fractal ping ntp telnet

#------------------------------------------------------------------------------------
# build common IP stack objects
#------------------------------------------------------------------------------------
#ip: ipnetif ipcore
#
#ipcore: $(COREOBJ)
#ipnetif: $(NETIFOBJ)
#ipnetwork: $(NETWORKOBJ)
#iptransport: $(TRANSPORTOBJ)

#------------------------------------------------------------------------------------
# disktest.exe, a fixed disk test utility
#------------------------------------------------------------------------------------
disktest: disktest.exe

disktest.exe: disktest.o
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@
#	./makeimg.sh $@ $(FLPIMG)

#------------------------------------------------------------------------------------
# xmodem.exe, an Xmodem upload and download utility
#------------------------------------------------------------------------------------
xmodem: xmodem.exe

xmodem.exe: xmodem.o crc16.o
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# fractal.exe, draw Mandelbrot fractal using INT10 graphics BIOS calls
#------------------------------------------------------------------------------------
fractal: fractal.exe

fractal.exe: fractal.o
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# ntp.exe, update system time and date with NTP
#------------------------------------------------------------------------------------
ntp: ntp.exe

ntp.exe: ntp.o $(COREOBJ) $(NETIFOBJ) $(NETWORKOBJ) $(TRANSPORTOBJ)
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# ping.exe, network PING client
#------------------------------------------------------------------------------------
ping: ping.exe

ping.exe: ping.o $(COREOBJ) $(NETIFOBJ) $(NETWORKOBJ) $(TRANSPORTOBJ)
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# telnet.exe, TELNET client
#------------------------------------------------------------------------------------
telnet: telnet.exe

telnet.exe: telnet.o $(COREOBJ) $(NETIFOBJ) $(NETWORKOBJ) $(TRANSPORTOBJ)
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# int25.exe, INT 25 read sector test
#------------------------------------------------------------------------------------
int25: int25.exe

int25.exe: int25.o
	$(LINK) $(LINKCFG) FILE $(subst $(SPC),$(COM),$(notdir $^)) NAME $@

#------------------------------------------------------------------------------------
# cleanup
#------------------------------------------------------------------------------------

.PHONY: clean

clean:
	rm -f *.exe
	rm -f *.o
	rm -f *.lst
	rm -f *.map
	rm -f *.lib
	rm -f *.bak
	rm -f *.cap
	rm -f *.err


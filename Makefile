MKDIR := mkdir -p

RM := rm -rf

#ZXNEXT_LAYER2 := ..
#ZXNEXT_LAYER2_INCLUDE := $(ZXNEXT_LAYER2)/include

LIBS := -llib/zxn/zxnext_layer2 -llib/zxn/zxnext_sprite

CFLAGS= +zxn -subtype=nex --lstcwd -clib=sdcc_iy -SO3 --max-allocs-per-node200000 $(LIBS) -preserve
#LDFLAGS= -m -clib=sdcc_iy $(CLEAN) $(LIBS)


DEBUGFLAGS := --list --c-code-in-asm

all: all_sdcc_iy

all_sdcc_iy:
	#zcc +zxn -subtype=nex -vn -SO3 -startup=1 -clib=sdcc_iy -m $(DEBUG) --lstcwd --max-allocs-per-node200000 -L$(LIBS) -lzxnext_layer2 test.c -o test -create-app
	zcc $(CFLAGS) test.c -o test -create-app

debug_sdcc_iy: DEBUG = $(DEBUGFLAGS)

debug_sdcc_iy: all_sdcc_iy

clean:
	$(RM) bin zcc_opt.def zcc_proj.lst *.lis

SYNCDIR   := C:\nextsync
sync:
	cp "test.nex" "$(SYNCDIR)/home/"
	# Warning: kills all running python processes
	# Remove these two lines and start the server manually if that bothers you
	-taskkill /F /IM python3.9.exe
	CMD /C start /d $(SYNCDIR) /min python3.9.exe nextsync.py


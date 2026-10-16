# PKG Magaza - OpenOrbis PS4 Toolchain Makefile (Linux / WSL)
# Kullanim:  export OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain ; make

TOOLCHAIN   ?= $(OO_PS4_TOOLCHAIN)
TITLE       := PKG Magaza
VERSION     := 01.02
TITLE_ID    := PKGM00001
CONTENT_ID  := IV0000-PKGM00001_00-PKGMAGAZA0000000

# SDL2 portu baglanti hatasi verirse eksik kutuphaneyi toolchain'in SDL ornegindeki
# Makefile'dan buraya ekleyin.
LIBS        := -lc -lkernel -lc++ -lSDL2 -lSceVideoOut -lSceAudioOut -lScePad \
               -lSceUserService -lSceSystemService -lSceSysmodule -lSceNet \
               -lSceSsl -lSceHttp

ODIR        := build
SDIR        := src
# -include oo_compat.h: standart basliklardan once nanosleep'i bildirir
# (OpenOrbis libc++ <atomic> bunu arar).
EXTRAFLAGS  := -Iinclude -O2 -Wall -Wno-unused-function -MMD -MP \
               -DAPP_VERSION='"$(VERSION)"' -include include/oo_compat.h

# GoldHEN Plugins SDK: korumali alandan cikis (disk okuma icin gerekli).
#   make GOLDHEN_SDK=/opt/GoldHEN_Plugins_SDK
GOLDHEN_SDK ?=
ifneq ($(GOLDHEN_SDK),)
EXTRAFLAGS  += -DUSE_GOLDHEN_SDK -I$(GOLDHEN_SDK)/include
LIBS        += -L$(GOLDHEN_SDK) -lGoldHEN_Hook
endif

CC          := clang
CXX         := clang++
LD          := ld.lld
CDIR        := linux

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1 -std=c++17
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

CPPFILES    := $(wildcard $(SDIR)/*.cpp)
OBJS        := $(patsubst $(SDIR)/%.cpp,$(ODIR)/%.o,$(CPPFILES))
ASSETS      := assets/font.ttf assets/font-bold.ttf
SCE_SYS     := sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic1.png

.PHONY: all clean

all: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .

pkg.gp4: eboot.bin $(SCE_SYS) $(ASSETS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

sce_sys/param.sfo: Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

eboot.bin: $(OBJS)
	$(LD) $(OBJS) -o $(ODIR)/app.elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(ODIR)/app.elf -out=$(ODIR)/app.oelf --eboot "eboot.bin" --paid 0x3800000000000011

$(ODIR)/%.o: $(SDIR)/%.cpp | $(ODIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(ODIR):
	mkdir -p $@

clean:
	rm -rf $(ODIR) eboot.bin pkg.gp4 sce_sys/param.sfo *.pkg

-include $(OBJS:.o=.d)

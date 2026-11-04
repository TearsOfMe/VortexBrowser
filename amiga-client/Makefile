CC      = /opt/ppc-amigaos/bin/ppc-amigaos-gcc
STRIP   = /opt/ppc-amigaos/bin/ppc-amigaos-strip
SDK     = /opt/ppc-amigaos/ppc-amigaos/SDK

# Universal AmigaOS 4 PowerPC baseline (Compatible with ALL AmigaOS 4 machines:
# AmigaOne X5000, A1222/A1222+, Sam460, Sam440, G4, G3, Classic PPC)
# STRICTLY NO -maltivec, NO -mabi=altivec, NO -mcpu=7450!
OPTIMIZE = -O2 -mcpu=powerpc \
           -fomit-frame-pointer -fno-strict-aliasing \
           -finline-functions -fweb -frename-registers $(DEBUG_CFLAGS)

ifeq ($(DEBUG),1)
  DEBUG_CFLAGS = -DDEBUG -g
else
  DEBUG_CFLAGS = -DNDEBUG
endif

CFLAGS  = $(OPTIMIZE) -Wall -Wno-deprecated-declarations \
          -I../common \
          -I./src \
          -I$(SDK)/include/include_h \
          -I$(SDK)/include/netinclude \
          -I$(SDK)/local/common/include \
          -I$(SDK)/local/newlib/include

LDFLAGS = -L$(SDK)/local/newlib/lib -llz4 -ljpeg -lauto

SRCS_COMMON = src/net.c src/renderer.c src/favorites.c src/download.c src/audio.c src/prefs.c src/clipboard.c src/passwords.c
OBJS_COMMON = $(SRCS_COMMON:.c=.o)

OBJS_RX     = src/main.o src/gui.o src/gui_prefs.o src/gui_passwords.o src/gui_download.o $(OBJS_COMMON)
TARGET_RX   = VortexBrowser

all: $(TARGET_RX)

$(TARGET_RX): $(OBJS_RX)
	$(CC) $(OBJS_RX) -o $(TARGET_RX) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET_RX) -o $(TARGET_RX).stripped

clean:
	rm -f src/*.o $(TARGET_RX) $(TARGET_RX).stripped

.PHONY: all clean

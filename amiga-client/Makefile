CC      = /opt/ppc-amigaos/bin/ppc-amigaos-gcc
STRIP   = /opt/ppc-amigaos/bin/ppc-amigaos-strip
SDK     = /opt/ppc-amigaos/ppc-amigaos/SDK

# 1. Universal AmigaOS 4 PowerPC baseline (Compatible with ALL AmigaOS 4 machines:
# AmigaOne X5000, A1222/A1222+, Sam460, Sam440, G4, G3, Classic PPC)
# STRICTLY NO -maltivec, NO -mabi=altivec, NO -mcpu=7450!
OPTIMIZE = -O2 -mcpu=powerpc \
           -fomit-frame-pointer -fno-strict-aliasing \
           -finline-functions -fweb -frename-registers $(DEBUG_CFLAGS)

# 2. Pure AltiVec flags (for dedicated X1000 and G4 build)
OPTIMIZE_ALTIVEC = -O3 -mcpu=7450 -maltivec -mabi=altivec \
                   -fomit-frame-pointer -fno-strict-aliasing \
                   -finline-functions -fweb -frename-registers $(DEBUG_CFLAGS)

ifeq ($(DEBUG),1)
  DEBUG_CFLAGS = -DDEBUG -g
else
  DEBUG_CFLAGS = -DNDEBUG
endif

INCLUDES = -I../common \
           -I./src \
           -I../sdk_va \
           -I$(SDK)/include/include_h \
           -I$(SDK)/include/netinclude \
           -I$(SDK)/local/common/include \
           -I$(SDK)/local/newlib/include

CFLAGS         = $(OPTIMIZE) -Wall -Wno-deprecated-declarations -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 $(INCLUDES)
CFLAGS_ALTIVEC = $(OPTIMIZE_ALTIVEC) -Wall -Wno-deprecated-declarations -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 $(INCLUDES)

LDFLAGS = -L../sdk_va -L$(SDK)/local/newlib/lib -lavcodec -lavutil -lva -llz4 -ljpeg -lauto

SRCS_COMMON = src/net.c src/renderer.c src/favorites.c src/download.c src/audio.c src/prefs.c src/clipboard.c src/passwords.c src/locale_strings.c
SRCS_ALL    = src/main.c src/gui.c src/gui_prefs.c src/gui_passwords.c src/gui_download.c $(SRCS_COMMON)
HDRS_ALL    = $(wildcard src/*.h ../common/*.h)

# Universal objects (Hybrid: scalar app code + isolated AltiVec SIMD unit for dynamic dispatch)
OBJS_UNIVERSAL = $(SRCS_ALL:.c=.o) src/renderer_altivec.o

# Dedicated Pure AltiVec objects
OBJS_ALTIVEC_DIR = obj_altivec
OBJS_PURE_ALTIVEC = $(patsubst src/%.c,$(OBJS_ALTIVEC_DIR)/%.o,$(SRCS_ALL)) $(OBJS_ALTIVEC_DIR)/renderer_altivec.o

TARGET_UNIVERSAL = VortexRemoteBrowser
TARGET_ALTIVEC   = VortexRemoteBrowser-AltiVec

all: catalogs $(TARGET_UNIVERSAL) $(TARGET_ALTIVEC)

catalogs:
	@mkdir -p catalogs/english catalogs/german catalogs/deutsch catalogs/french catalogs/francais catalogs/italian catalogs/italiano catalogs/spanish catalogs/espanol
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/english/VortexRemoteBrowser.ct CATALOG=catalogs/english/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/german/VortexRemoteBrowser.ct CATALOG=catalogs/german/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/deutsch/VortexRemoteBrowser.ct CATALOG=catalogs/deutsch/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/french/VortexRemoteBrowser.ct CATALOG=catalogs/french/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/francais/VortexRemoteBrowser.ct CATALOG=catalogs/francais/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/italian/VortexRemoteBrowser.ct CATALOG=catalogs/italian/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/italiano/VortexRemoteBrowser.ct CATALOG=catalogs/italiano/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/spanish/VortexRemoteBrowser.ct CATALOG=catalogs/spanish/VortexRemoteBrowser.catalog NOOPTIM
	flexcat catalogs/VortexRemoteBrowser.cd catalogs/espanol/VortexRemoteBrowser.ct CATALOG=catalogs/espanol/VortexRemoteBrowser.catalog NOOPTIM

$(TARGET_UNIVERSAL): $(OBJS_UNIVERSAL)
	$(CC) $(OBJS_UNIVERSAL) -o $(TARGET_UNIVERSAL) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET_UNIVERSAL) -o $(TARGET_UNIVERSAL).stripped

src/renderer_altivec.o: src/renderer_altivec.c src/renderer_altivec.h $(HDRS_ALL)
	$(CC) $(CFLAGS_ALTIVEC) -c $< -o $@

src/%.o: src/%.c $(HDRS_ALL)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_ALTIVEC_DIR):
	mkdir -p $(OBJS_ALTIVEC_DIR)

$(OBJS_ALTIVEC_DIR)/%.o: src/%.c $(HDRS_ALL) | $(OBJS_ALTIVEC_DIR)
	$(CC) $(CFLAGS_ALTIVEC) -c $< -o $@

$(TARGET_ALTIVEC): $(OBJS_PURE_ALTIVEC)
	$(CC) $(OBJS_PURE_ALTIVEC) -o $(TARGET_ALTIVEC) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET_ALTIVEC) -o $(TARGET_ALTIVEC).stripped

clean:
	rm -rf src/*.o $(OBJS_ALTIVEC_DIR) $(TARGET_UNIVERSAL) $(TARGET_UNIVERSAL).stripped $(TARGET_ALTIVEC) $(TARGET_ALTIVEC).stripped

.PHONY: all clean catalogs

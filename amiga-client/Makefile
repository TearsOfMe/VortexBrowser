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

# Universal objects (Hybrid: scalar app code + isolated AltiVec SIMD unit for dynamic dispatch)
OBJS_UNIVERSAL = $(SRCS_ALL:.c=.o) src/renderer_altivec.o

# Dedicated Pure AltiVec objects
OBJS_ALTIVEC_DIR = obj_altivec
OBJS_PURE_ALTIVEC = $(patsubst src/%.c,$(OBJS_ALTIVEC_DIR)/%.o,$(SRCS_ALL)) $(OBJS_ALTIVEC_DIR)/renderer_altivec.o

TARGET_UNIVERSAL = VortexBrowser
TARGET_ALTIVEC   = VortexBrowser-AltiVec

all: catalogs $(TARGET_UNIVERSAL) $(TARGET_ALTIVEC)

catalogs:
	@mkdir -p catalogs/deutsch catalogs/english catalogs/italian catalogs/italiano catalogs/french catalogs/francais
	flexcat catalogs/VortexBrowser.cd catalogs/deutsch/VortexBrowser.ct CATALOG=catalogs/deutsch/VortexBrowser.catalog NOOPTIM
	flexcat catalogs/VortexBrowser.cd catalogs/english/VortexBrowser.ct CATALOG=catalogs/english/VortexBrowser.catalog NOOPTIM
	flexcat catalogs/VortexBrowser.cd catalogs/italian/VortexBrowser.ct CATALOG=catalogs/italian/VortexBrowser.catalog NOOPTIM
	flexcat catalogs/VortexBrowser.cd catalogs/italiano/VortexBrowser.ct CATALOG=catalogs/italiano/VortexBrowser.catalog NOOPTIM
	flexcat catalogs/VortexBrowser.cd catalogs/french/VortexBrowser.ct CATALOG=catalogs/french/VortexBrowser.catalog NOOPTIM
	flexcat catalogs/VortexBrowser.cd catalogs/francais/VortexBrowser.ct CATALOG=catalogs/francais/VortexBrowser.catalog NOOPTIM

$(TARGET_UNIVERSAL): $(OBJS_UNIVERSAL)
	$(CC) $(OBJS_UNIVERSAL) -o $(TARGET_UNIVERSAL) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET_UNIVERSAL) -o $(TARGET_UNIVERSAL).stripped

src/renderer_altivec.o: src/renderer_altivec.c src/renderer_altivec.h
	$(CC) $(CFLAGS_ALTIVEC) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_ALTIVEC_DIR):
	mkdir -p $(OBJS_ALTIVEC_DIR)

$(OBJS_ALTIVEC_DIR)/%.o: src/%.c | $(OBJS_ALTIVEC_DIR)
	$(CC) $(CFLAGS_ALTIVEC) -c $< -o $@

$(TARGET_ALTIVEC): $(OBJS_PURE_ALTIVEC)
	$(CC) $(OBJS_PURE_ALTIVEC) -o $(TARGET_ALTIVEC) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET_ALTIVEC) -o $(TARGET_ALTIVEC).stripped

clean:
	rm -rf src/*.o $(OBJS_ALTIVEC_DIR) $(TARGET_UNIVERSAL) $(TARGET_UNIVERSAL).stripped $(TARGET_ALTIVEC) $(TARGET_ALTIVEC).stripped

.PHONY: all clean

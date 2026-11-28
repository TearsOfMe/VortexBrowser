CC      = /opt/ppc-amigaos/bin/ppc-amigaos-gcc
STRIP   = /opt/ppc-amigaos/bin/ppc-amigaos-strip
SDK     = /opt/ppc-amigaos/ppc-amigaos/SDK

OPTIMIZE = -O2 -mcpu=powerpc -fomit-frame-pointer -fno-strict-aliasing -finline-functions
INCLUDES = -I./ffout/include \
           -I../sdk_va \
           -I$(SDK)/include/include_h \
           -I$(SDK)/include/netinclude \
           -I$(SDK)/local/common/include \
           -I$(SDK)/local/newlib/include

CFLAGS  = $(OPTIMIZE) -Wall -Wno-deprecated-declarations $(INCLUDES)
LDFLAGS = -L./ffout/lib -L$(SDK)/local/newlib/lib \
          -lavformat -lavcodec -lswresample -lavutil \
          -lssl -lcrypto -lz -lbz2 -lpthread -lauto

TARGET  = playerintern

all: $(TARGET)

$(TARGET): main.c ../sdk_va/va_stubs.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c ../sdk_va/va_stubs.c $(LDFLAGS)
	$(STRIP) -s $(TARGET) -o $(TARGET).stripped

clean:
	rm -f $(TARGET) $(TARGET).stripped *.o

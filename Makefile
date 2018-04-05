# Mostly copied from hello_pi
CFLAGS := -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi
LDFLAGS := -L/opt/vc/lib/ -lbrcmGLESv2 -lbrcmEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lm -L/opt/vc/src/hello_pi/libs/ilclient -Wl,--no-whole-archive -rdynamic
INCLUDES := -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux

SRC := $(wildcard *.c)
HEADERS := $(wildcard *.h)
OBJECTS := $(SRC:.c=.o)

.PHONY: all clean
all: hud
clean:
	rm -f hud *.o
hud: $(OBJECTS)
	$(CC) -o hud $(LDFLAGS) $(OBJECTS)
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

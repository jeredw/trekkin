CC=g++
CFLAGS=-Wall -g -O3 -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux 
LDFLAGS=-L/opt/vc/lib -lGLESv2 -lEGL -lopenmaxil -lbcm_host -luv
SRC := $(wildcard *.cc)
OBJECTS := $(SRC:.cc=.o)

.PHONY: all clean
all: trekkin
clean:
	rm -f trekkin *.o
trekkin: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
%.o: %.cc
	$(CC) -c $(CFLAGS) $< -o $@

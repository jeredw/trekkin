CC=g++
#ASAN=-fsanitize=address
CFLAGS=-std=c++11 -Wall -g $(ASAN) -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux `sdl2-config --cflags`
LDFLAGS=$(ASAN) -L/opt/vc/lib -lGLESv2 -lEGL -lopenmaxil -lbcm_host -L/usr/lib -luv -lstdc++ `sdl2-config --libs` -lSDL2_mixer
SRC := $(wildcard *.cc)
OBJECTS := $(SRC:.cc=.o)

.PHONY: all clean format
all: trekkin
clean:
	rm -f trekkin *.o
format:
	clang-format-3.5 -style=Google -i *.cc *.h
trekkin: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
%.o: %.cc
	$(CC) -c $(CFLAGS) $< -o $@

CC := cc
CFLAGS := -std=c11
LD := cc
LDFLAGS :=
SRC := $(wildcard *.c)
HEADERS := $(wildcard *.h)
OBJECTS := $(SRC:.c=.o)

.PHONY: all clean
all: hud
clean:
	rm -f hud *.o
hud: $(OBJECTS)
	$(LD) -o hud $(LDFLAGS) $(OBJECTS)
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

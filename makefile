CC      = gcc
CFLAGS  = -std=c99 -Wall -Werror
LDFLAGS =

OUTPUT = bfoc

SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)

all: $(OUTPUT)

$(OUTPUT): $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OUTPUT) $(OBJECTS)

CFLAGS := $(shell curl-config --cflags)
CFLAGS += $(shell pkg-config --with-path=. --cflags myhtml)
CFLAGS += -Wall -g

LDFLAGS := $(shell curl-config --libs)
LDFLAGS += $(shell pkg-config --with-path=. --libs myhtml)

archive: main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c $^

.PHONY: clean

clean: 
	-rm -f main.o archive

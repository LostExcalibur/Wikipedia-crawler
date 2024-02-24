CC := clang

CFLAGS := $(shell curl-config --cflags)
CFLAGS += $(shell pkg-config --with-path=. --cflags myhtml)
CFLAGS += -Wall -g

LDFLAGS := $(shell curl-config --libs)
LDFLAGS += $(shell pkg-config --with-path=. --libs myhtml)

archive: main.o queue.o hashset.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@

.PHONY: clean

clean: 
	-rm -f queue.o hashset.o main.o archive

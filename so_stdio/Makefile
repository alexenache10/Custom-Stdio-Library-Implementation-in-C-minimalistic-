LIB_NAME = libso_stdio.so
SOURCE_FILE = so_stdio.c
CC = gcc
CFLAGS = -Wall -fPIC

$(LIB_NAME): $(SOURCE_FILE)
	$(CC) $(CFLAGS) -shared -o $@ $<

build: $(LIB_NAME)

clean:
	rm -f $(LIB_NAME)


.PHONY: clean build

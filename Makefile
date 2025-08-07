# Compiler & Flags
CC = gcc
CFLAGS = -g -pthread

# Source & Object Files
SRCS = nfs_manager.c mqueue.c nfs_console.c nfs_client.c
OBJS = $(SRCS:.c=.o)

# Executables
BINS = nfs_manager nfs_console nfs_client

##.PHONY: all build clean

# Default target
all: $(BINS)

# Build all binaries
build: $(BINS)

# Rules for individual binaries
#nfs_console: nfs_console.o
#	$(CC) $(CFLAGS) -o $@ $^

nfs_manager: nfs_manager.o mqueue.o
	$(CC) $(CFLAGS) -o $@ $^

nfs_console: nfs_console.o
	$(CC) $(CFLAGS) -o $@ $^

nfs_client: nfs_client.o
	$(CC) $(CFLAGS) -o $@ $^

#worker: worker.o
#	$(CC) $(CFLAGS) -o $@ $^

# Generic rule for compiling .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

# Clean build artifacts
clean:
	rm -rf *.dSYM $(BINS)

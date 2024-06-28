# Makefile for building the DRM application

# Compiler settings
CC = aarch64-linux-gcc
SYSROOT = /* Your Linux root directory */ 
INCLUDE_DIRS = -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/libdrm
CFLAGS = -mcpu=cortex-a72+crc+crypto -fstack-protector-strong -fPIE -pie \
         -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security \
         -fPIC --sysroot=$(SYSROOT) $(INCLUDE_DIRS) -O2
LDFLAGS = -ldrm

# Target binary
TARGET = drm_test

# Source files
SRCS = drm_test.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Rule to build the target binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Rule to build object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(OBJS) $(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: clean all


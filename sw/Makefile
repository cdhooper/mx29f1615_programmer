CC=gcc
CFLAGS=-O2 -g -pthread -lpthread -Wall -Wpedantic
PROG=mxprog

ifeq ($(OS),Windows_NT)
    CFLAGS += -DWIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CFLAGS += -DLINUX
    endif
    ifeq ($(UNAME_S),Darwin)
#        CFLAGS += -DOSX -target x86_64-apple-macos10.12
        CFLAGS += -DOSX -lxml2 -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/libxml2
    endif
endif

$(PROG): Makefile

$(PROG): mxprog.c | $(USB_HDR)
	$(CC) $(CFLAGS) -o $@ $<

$(USB_HDR):
	echo "You must install the libusb development package"
	echo "On Fedora:   dnf install libusb-devel"
	echo "On Raspbian: apt-get install libusb-dev"
	exit 1

clean:
	rm -f $(PROG)

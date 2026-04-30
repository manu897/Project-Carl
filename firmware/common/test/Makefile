# Host-side unit tests for firmware/common/.
# Requires mbedtls 3.x:
#   macOS:  brew install mbedtls
#   Debian: apt-get install libmbedtls-dev

CXX      ?= clang++
CXXSTD    = -std=c++17
WARN      = -Wall -Wextra -Wpedantic -Werror
OPT       = -O2

# Resolve mbedtls install location. pkg-config gives include path; we always
# link all three libs since the AES-CCM symbols live in libmbedcrypto, not
# libmbedtls itself, and pkg-config on brew only emits -lmbedtls.
PKG_CFLAGS := $(shell pkg-config --cflags mbedtls 2>/dev/null)
LDLIBS_MBEDTLS := -lmbedcrypto -lmbedx509 -lmbedtls

BREW_PREFIX := $(shell brew --prefix mbedtls 2>/dev/null)
ifneq ($(strip $(BREW_PREFIX)),)
  PKG_CFLAGS  := -I$(BREW_PREFIX)/include
  PKG_LDFLAGS := -L$(BREW_PREFIX)/lib $(LDLIBS_MBEDTLS)
else
  PKG_LDFLAGS := $(LDLIBS_MBEDTLS)
endif

INCLUDE = -I.. -I../bthome $(PKG_CFLAGS)
LDFLAGS = $(PKG_LDFLAGS)

SRC = bthome_test.cpp \
      ../bthome/bthome.cpp \
      ../bthome/bthome_decode.cpp \
      ../bthome/aes_ccm.cpp

bthome_test: $(SRC) ../bthome/bthome.h ../bthome/bthome_decode.h ../bthome/aes_ccm.h
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INCLUDE) $(SRC) -o $@ $(LDFLAGS)

test: bthome_test
	./bthome_test

clean:
	rm -f bthome_test

.PHONY: test clean

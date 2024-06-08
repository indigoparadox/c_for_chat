
# vim: ft=make noexpandtab

CCHAT_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c src/webutil.c

PKG_CFG_DEPS := sqlite3 libcurl openssl libcrypto

CFLAGS_DEBUG := -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

INCLUDES := $(shell pkg-config $(PKG_CFG_DEPS) --cflags) -I/usr/local/include

LDFLAGS_DEBUG := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LIBS := $(shell pkg-config $(PKG_CFG_DEPS) --libs) -lfcgi

LIBS_STATIC_DEPS_COMMON := -lz -ldl -lpthread -lpsl -lnghttp2 -lidn2 -lunistring

LIBS_STATIC_DEPS_FREEBSD := -lm $(LIBS_STATIC_DEPS_COMMON)

LIBS_STATIC_DEPS_ALPINE := $(LIBS_STATIC_DEPS_COMMON) -lzstd -lcares -lbrotlidec -lbrotlicommon -static-libgcc

ifeq ("$(shell uname -o)", "FreeBSD")
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_FREEBSD)
else
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_ALPINE)
endif

ifeq ("$(BUILD)", "STATIC")
CFLAGS := $(INCLUDES)
LDFLAGS := -static $(LIBS) $(LIBS_STATIC_DEPS)
else
ifeq ("$(BUILD)", "DEBUG")
CFLAGS := $(CFLAGS_DEBUG) $(INCLUDES)
LDFLAGS := $(LDFLAGS_DEBUG) $(LIBS)
else
CFLAGS := $(INCLUDES)
LDFLAGS := $(LIBS)
endif
endif

.PHONY: clean

all: cchat

cchat: $(addprefix obj/,$(subst .c,.o,$(CCHAT_SOURCES)))
	$(CC) -o $@ $^ $(LDFLAGS)

obj/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj



# vim: ft=make noexpandtab

CFLAGS :=
LDFLAGS :=

CCHAT_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c src/webutil.c src/rtproto.c

PKG_CFG_DEPS := sqlite3 libcurl openssl libcrypto libwebsockets

CFLAGS_DEBUG := -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

INCLUDES := $(shell pkg-config $(PKG_CFG_DEPS) --cflags) -I/usr/local/include

LDFLAGS_DEBUG := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LIBS := $(shell pkg-config $(PKG_CFG_DEPS) --libs) -lfcgi -lpthread

LIBS_STATIC_DEPS_COMMON := -lz -ldl -lpsl -lnghttp2 -lidn2 -lunistring

LIBS_STATIC_DEPS_FREEBSD := -lm $(LIBS_STATIC_DEPS_COMMON)

LIBS_STATIC_DEPS_ALPINE := $(LIBS_STATIC_DEPS_COMMON) -lzstd -lcares -lbrotlidec -lbrotlicommon -static-libgcc

ifeq ("$(shell uname -o)", "FreeBSD")
CFLAGS += -Wno-ignored-attributes
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_FREEBSD)
else
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_ALPINE)
endif

ifeq ("$(BUILD)", "STATIC")
CFLAGS += $(INCLUDES)
LDFLAGS += -static $(LIBS) $(LIBS_STATIC_DEPS)
STRIP_CMD := strip cchat
else
ifeq ("$(BUILD)", "DEBUG")
CFLAGS += $(CFLAGS_DEBUG) $(INCLUDES)
LDFLAGS += $(LDFLAGS_DEBUG) $(LIBS)
else
CFLAGS += $(INCLUDES)
LDFLAGS += $(LIBS)
STRIP_CMD := strip cchat
endif
endif

.PHONY: clean

all: cchat

cchat: $(addprefix obj/,$(subst .c,.o,$(CCHAT_SOURCES)))
	$(CC) -o $@ $^ $(LDFLAGS)
	$(STRIP_CMD)

obj/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj


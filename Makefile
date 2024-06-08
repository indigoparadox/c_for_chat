
# vim: ft=make noexpandtab

CCHAT_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c src/webutil.c

PKG_CFG_DEPS := sqlite3 libcurl openssl libcrypto

CFLAGS_DEBUG := -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

INCLUDES := $(shell pkg-config $(PKG_CFG_DEPS) --cflags)

LDFLAGS_DEBUG := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LIBS := $(shell pkg-config $(PKG_CFG_DEPS) --libs) -lfcgi

ifeq ("$(BUILD)", "STATIC")
CFLAGS := $(INCLUDES)
LDFLAGS := -static $(LIBS) -lz -ldl -lpthread -lpsl -lnghttp2 -lidn2 -lzstd -lcares -lunistring -lbrotlidec -lbrotlicommon -static-libgcc
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


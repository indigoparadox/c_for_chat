
# vim: ft=make noexpandtab

CFLAGS := -fstack-protector-all -fPIE -D_FORTIFY_SOURCE=3 -fcf-protection=full
LDFLAGS :=

ASSETS := style.css alert.mp3 chat.js strftime.js

CCHAT_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c src/webutil.c src/rtproto.c src/assets.c

PKG_CFG_DEPS := sqlite3 libcrypto libwebsockets libuv openssl

LIBS_STATIC_DEPS_COMMON := 

ifneq ("$(RECAPTCHA)", "NO")
PKG_CFG_DEPS += libcurl
CFLAGS += -DUSE_RECAPTCHA
LIBS_STATIC_DEPS_COMMON += -lz -ldl -lpsl -lnghttp2 -lidn2 -lunistring
endif

CFLAGS_DEBUG := -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

INCLUDES := $(shell pkg-config $(PKG_CFG_DEPS) --cflags) -I/usr/local/include

LDFLAGS_DEBUG := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LIBS := $(shell pkg-config $(PKG_CFG_DEPS) --libs) -lfcgi -lpthread

LIBS_STATIC_DEPS_FREEBSD := -lm $(LIBS_STATIC_DEPS_COMMON)

LIBS_STATIC_DEPS_ALPINE := $(LIBS_STATIC_DEPS_COMMON) -ldl -lm

LIBS_STATIC_DEPS_UBUNTU := $(LIBS_STATIC_DEPS_COMMON) -lev -lcap -ldl -lm

ifneq ("$(RECAPTCHA)", "NO")
LIBS_STATIC_DEPS_ALPINE += -lzstd -lcares -lbrotlidec -lbrotlicommon
endif

LIBS_STATIC_DEPS_ALPINE += -static-libgcc

ifeq ("$(shell uname -o)", "FreeBSD")
CFLAGS += -Wno-ignored-attributes
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_FREEBSD)
else
ifeq ("$(findstring Ubuntu 20,$(shell grep Ubuntu\\\ 20 /etc/os-release))", "Ubuntu 20")
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_UBUNTU)
CFLAGS += -pie -DUSE_LWS_OLD_RETRY
else
LIBS_STATIC_DEPS := $(LIBS_STATIC_DEPS_ALPINE)
CFLAGS += -pie
endif
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

assets/%.h: %
	mkdir -p $(dir $@)
	xxd -i $< > $@

obj/%.o: %.c | $(addprefix assets/,$(addsuffix .h,$(ASSETS)))
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj assets


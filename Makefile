
# vim: ft=make noexpandtab

CCHAT_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c

PKG_CFG_DEPS := sqlite3 libcurl openssl libcrypto

CFLAGS := $(shell pkg-config $(PKG_CFG_DEPS) --cflags) -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LDFLAGS := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address $(shell pkg-config $(PKG_CFG_DEPS) --libs) -lfcgi

.PHONY: clean

all: cchat

cchat: $(addprefix obj/,$(subst .c,.o,$(CCHAT_SOURCES)))
	$(CC) -o $@ $^ $(LDFLAGS)

obj/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj


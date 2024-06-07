
# vim: ft=make noexpandtab

FLOPPCGI_SOURCES := src/main.c src/bstrlib.c src/cchat.c src/chatdb.c src/bcgi.c

CFLAGS := -DDEBUG -Wall -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

LDFLAGS := -g -fsanitize=undefined -fsanitize=leak -fsanitize=address

.PHONY: clean

all: cchat

cchat: $(addprefix obj/,$(subst .c,.o,$(FLOPPCGI_SOURCES)))
	$(CC) -o $@ $^ $(LDFLAGS) -lfcgi -lsqlite3 -lssl -lcrypto

obj/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj


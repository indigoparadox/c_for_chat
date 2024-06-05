
# vim: ft=make noexpandtab

FLOPPCGI_SOURCES := src/main.c src/bstrlib.c src/cchat.c

CFLAGS := -DDEBUG -Wall -g

LDFLAGS := -g

.PHONY: clean

all: cchat

cchat: $(addprefix obj/,$(subst .c,.o,$(FLOPPCGI_SOURCES)))
	$(CC) -o $@ $^ $(LDFLAGS) -lfcgi

obj/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -rf cchat obj


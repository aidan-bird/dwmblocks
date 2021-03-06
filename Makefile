PREFIX ?= /usr/local

libs=`pkg-config --cflags x11` `pkg-config --libs x11` `pkg-config --cflags --libs libnotify`

debug: dwmblocks.c blocks.h
	cc $(libs) -ggdb3 -Og dwmblocks.c -o debug 

output: dwmblocks.c blocks.h
	cc $(libs) -O2 -march=native dwmblocks.c -o dwmblocks 
clean:
	rm -f *.o *.gch dwmblocks debug
install: output
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwmblocks $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwmblocks
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwmblocks

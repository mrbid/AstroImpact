INCLUDEFLAGS = -I.

CFLAGS += $(INCLUDEFLAGS) -Ofast -DSEIR_RAND

ifneq ($(generic), true)
	CFLAGS += -march=native
endif

ifeq ($(debug), true)
	CFLAGS += -DVERBOSE -DDEBUG_GL
endif

ifeq ($(test), true)
	CFLAGS += -DFAST_START
endif

LDFLAGS = -lm

DEPS = $(shell $(CC) $(INCLUDEFLAGS) -MM -MT $(1).o $(1).c | sed -z 's/\\\n //g')

.PHONY: all clean cleanall release
all: bin bin/fat bin/fatserver

bin:
	mkdir bin

$(call DEPS,client/main)
	$(CC) $(CFLAGS) -c $< -o $@

$(call DEPS,client/glad_gl)
	$(CC) $(CFLAGS) -c $< -o $@

# processing models.c is too slow, hardcode
#$(call DEPS,assets/models)
#	$(CC) $(CFLAGS) -c $< -o $@
assets/client_models.o: assets/models.c assets/models.h
	$(CC) $(CFLAGS) -c $< -o $@

$(call DEPS,inc/vec)
	$(CC) $(CFLAGS) -c $< -o $@

#$(call DEPS,inc/protocol)
#	$(CC) $(CFLAGS) -c $< -o $@

inc/munmap.o: inc/munmap.asm
	nasm -f elf64 $< -o $@

bin/fat: client/main.o client/glad_gl.o assets/client_models.o assets/images1.o assets/images2.o assets/images3.o assets/images4.o inc/vec.o
	$(CC) $^ $(LDFLAGS) -lglfw -lpthread -o $@


$(call DEPS,server/main)
	$(CC) $(CFLAGS) -c $< -o $@

$(call DEPS,server/utils)
	$(CC) $(CFLAGS) -c $< -o $@

assets/server_models.o: assets/models.c assets/models.h
	$(CC) $(CFLAGS) -c $< -o $@ -DEXO_VERTICES_ONLY


bin/fatserver: server/main.o server/utils.o assets/server_models.o inc/vec.o inc/munmap.o
	$(CC) $^ $(LDFLAGS) -o $@ -static


clean:
	$(RM) bin/fat bin/fatserver client/*.o server/*.o inc/*.o bin/*.upx

cleanall: clean
	$(RM) assets/*.o

release: bin/fat
	strip --strip-unneeded bin/fat
	upx --lzma --best bin/fat

fat.deb: deb/usr/games/fat deb/usr/games/fatserver
	dpkg-deb --build deb fat.deb

deb/usr/games/fat: bin/fat
	mkdir -p deb/usr/games
	cp bin/fat deb/usr/games/fat

deb/usr/games/fatserver: bin/fatserver
	mkdir -p deb/usr/games
	cp bin/fatserver deb/usr/games/fatserver


install:
	cp bin/fat $(DESTDIR)/fractalattackonline2

uninstall:
	rm $(DESTDIR)/fractalattackonline2

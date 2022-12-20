INCLUDEFLAGS = -I.

CFLAGS += $(INCLUDEFLAGS) -Ofast -DSEIR_RAND

ifneq ($(native), true)
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
all: bin bin/fat

bin:
	mkdir bin

$(call DEPS,client/main)
	$(CC) $(CFLAGS) -c $< -o $@

$(call DEPS,client/glad_gl)
	$(CC) $(CFLAGS) -c $< -o $@

assets/client_models.o: assets/models.c assets/models.h
	$(CC) $(CFLAGS) -c $< -o $@

$(call DEPS,inc/vec)
	$(CC) $(CFLAGS) -c $< -o $@

bin/fat: client/main.o client/glad_gl.o assets/client_models.o assets/images1.o assets/images2.o assets/images3.o assets/images4.o inc/vec.o
	$(CC) $^ $(LDFLAGS) -lglfw -lpthread -o $@

clean:
	$(RM) bin/fat client/*.o server/*.o inc/*.o bin/*.upx

cleanall: clean
	$(RM) assets/*.o

release: bin/fat
	strip --strip-unneeded bin/fat
	upx --lzma --best bin/fat

install:
	cp bin/fat $(DESTDIR)/fractalattackonline2

uninstall:
	rm $(DESTDIR)/fractalattackonline2

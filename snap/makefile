all:
	gcc -L/app/lib -I../ ../client/main.c ../client/glad_gl.c ../assets/models.c ../assets/images.c ../inc/vec.c -lm -lglfw -pthread -o astroimpact

install:
	cp astroimpact $(DESTDIR)

uninstall:
	rm $(DESTDIR)/astroimpact

clean:
	rm astroimpact
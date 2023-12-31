all:
	gcc -L/app/lib -I../ ../client/main.c ../client/glad_gl.c ../assets/models.c ../assets/images1.o ../assets/images2.o ../assets/images3.o ../assets/images4.o ../inc/vec.c -lm -lglfw -pthread -o astroimpact

install:
	cp astroimpact $(DESTDIR)

uninstall:
	rm $(DESTDIR)/astroimpact

clean:
	rm astroimpact
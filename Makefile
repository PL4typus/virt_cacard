IDIR = -I/usr/include/cacard -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/home/user/OpenSC/src/ -I/home/user/OpenSC
LIBS = -lcacard -lglib-2.0 -lopensc
CC = gcc
CFLAGS = $(IDIR) -Wall

ODIR = obj
_OBJ = cacard_tinker.o sc-test.o connection.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c
	$(CC) -c -g -o $@ $< $(CFLAGS)

$(ODIR)/sc-test.o: ../sc-test.c
	$(CC) -c -g -o $@ $< $(CFLAGS)

cacard_tinker.out: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

softhsm: 
	rm -rf db/ tokens/
	./setup-softhsm2.sh

all: 	clean cacard_tinker.out softhsm


.PHONY: clean

clean:
	rm -rf $(ODIR)/*.o *~ core db/ tokens/ *.out

IDIR = -I/usr/local/include/cacard -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include 
LIBS = -lcacard -lglib-2.0
CC = gcc
CFLAGS = $(IDIR) -Wall

ODIR = obj
_OBJ = virt_cacard.o connection.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c
	$(CC) -c -g -o $@ $< $(CFLAGS) 


virt_cacard.out: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

softhsm: 
	rm -rf db/ tokens/ softhsm2.conf
	./setup-softhsm2.sh

all: 	clean virt_cacard.out softhsm


.PHONY: clean

clean:
	rm -rf $(ODIR)/*.o *~ core *.out

softhsm_clean:
	rm -rf db/ tokens/ softhsm2.conf

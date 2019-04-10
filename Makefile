IDIR = -I/usr/include/cacard -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include 
LIBS = -lcacard -lglib-2.0
CC = gcc
CFLAGS = $(IDIR) -Wall -fsanitize=thread 

ODIR = obj
_OBJ = cacard_tinker.o connection.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c
	$(CC) -c -g -o $@ $< $(CFLAGS) 


cacard_tinker.out: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

softhsm: 
	rm -rf db/ tokens/ softhsm2.conf
	./setup-softhsm2.sh

all: 	clean cacard_tinker.out softhsm


.PHONY: clean

clean:
	rm -rf $(ODIR)/*.o *~ core *.out

softhsm_clean:
	rm -rf db/ tokens/ softhsm2.conf

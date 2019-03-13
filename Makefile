IDIR = -I/usr/include/cacard -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include
LIBS = -lcacard -lglib-2.0
CC = gcc
CFLAGS = $(IDIR) -Wall

ODIR = obj
_OBJ = cacard_tinker.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c
	$(CC) -c -g -o $@ $< $(CFLAGS)

cacard_tinker: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -rf $(ODIR)/*.o *~ core db/ tokens/

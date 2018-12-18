CC=gcc
CFLAGS=-Wall -DWOLFSSL_DTLS -DWOLFSSL_DEBUG -DTFM_TIMING_RESISTANT
EXE=ota-server

LIBS=-lwolfssl -lpthread

$(EXE): $(EXE).o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)

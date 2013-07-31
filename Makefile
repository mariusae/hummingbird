CFLAGS=-Wall

all: hstress hserve hplay

hstress: u.o hstress.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent
	
hserve: u.o hserve.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent

hplay: u.o hplay.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent

clean:
	rm -f hstress hserver hplay *.o

.PHONY: all clean

CFLAGS=-Wall

all: hstress hserve hplay

hstress: u.o hstress.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^
	
hserve: u.o hserve.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^

hplay: u.o hplay.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^

clean:
	rm -f hstress hserver hplay *.o

.PHONY: all clean
all: hb nectar

hb: u.o hummingbird.o
	$(CC) $(CFLAGS) -levent -o $@ $^
	
nectar: u.o nectar.o
	$(CC) $(CFLAGS) -levent -o $@ $^

clean:
	rm -f hb nectar *.o

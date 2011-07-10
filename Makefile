hb: hummingbird.c
	gcc $(CFLAGS) -levent -o hb hummingbird.c

clean:
	rm -f hb

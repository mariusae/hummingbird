hb: hummingbird.c
	gcc -levent -o hb hummingbird.c

clean:
	rm -f hb

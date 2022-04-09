
all: srv cli

srv: server.c
	gcc server.c -o srv -lrt -pthread

cli: client.c
	gcc client.c -o cli -lrt -pthread

clean:
	rm srv cli

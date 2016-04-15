CC = gcc
CFLAGS = -Wall -g -pthread -std=gnu99

all : client server

client : cproxy
server : sproxy

cproxy: cproxy.c proxy.h queue.c queue.h
	$(CC) cproxy.c queue.c -Wall -o cproxy $(CFLAGS)

sproxy: sproxy.c proxy.h queue.c queue.h
	$(CC) sproxy.c queue.c -Wall -o sproxy $(CFLAGS)

clean: 
	/bin/rm -f cproxy sproxy *.o
	
	
bridge-only:cproxy-working sproxy-working

cproxy-working: cproxy-working.c proxy.h
	${CC} ${CFLAGS} -o cproxy-bridiging cproxy-working.c
	
sproxy-working: sproxy-working.c proxy.h
	${CC} ${CFLAGS} -o sproxy-bridging sproxy-working.c


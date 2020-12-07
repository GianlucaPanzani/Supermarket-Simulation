
.PHONY: compile test testQ all clean

CC=gcc
CFLAGS=-Wall -pthread -g -lm

compile : all clean

test :
	./supermercato & \
	PROCESS_TO_KILL="$$!"; \
	(sleep 25 && kill -HUP $$PROCESS_TO_KILL); \
	wait $$PROCESS_TO_KILL; \
	echo ; \
	echo "LOGFILE RESULT:" && sleep 1; \
	bash analisi.sh logfile;

testQ :
	./supermercato & \
	PROCESS_TO_KILL="$$!";\
	(sleep 15 && kill -QUIT $$PROCESS_TO_KILL);\
	wait $$PROCESS_TO_KILL; \
	echo ; \
	echo "LOGFILE RESULT:" && sleep 1; \
	bash analisi.sh logfile;

all : supermercato.o cassa.o cliente.o myPthreads.o
	$(CC) $(CFLAGS) -o supermercato $^

supermercato.o : supermercato.c shared_data.h

cassa.o : cassa.c shared_data.h

cliente.o : cliente.c shared_data.h

myPthreads.o : myPthreads.c shared_data.h

clean :
	rm -f *.o *.~


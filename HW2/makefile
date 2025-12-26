CC=gcc
CFLAGS=-std=c99 -g -Wall -Werror -pedantic-errors -DNDEBUG -pthread

OBJS=main.o bank.o account.o util.o logger.o rwlock.o

bank: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -lm -o bank

main.o: main.c bank.h logger.h util.h
	$(CC) $(CFLAGS) -c main.c -o main.o

bank.o: bank.c bank.h account.h rwlock.h logger.h util.h
	$(CC) $(CFLAGS) -c bank.c -o bank.o

account.o: account.c account.h rwlock.h util.h
	$(CC) $(CFLAGS) -c account.c -o account.o

util.o: util.c util.h
	$(CC) $(CFLAGS) -c util.c -o util.o

logger.o: logger.c logger.h util.h
	$(CC) $(CFLAGS) -c logger.c -o logger.o

rwlock.o: rwlock.c rwlock.h util.h
	$(CC) $(CFLAGS) -c rwlock.c -o rwlock.o

clean:
	rm -f *.o bank log.txt

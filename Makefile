CC=gcc

LDFLAGS=-lrt

default: oss user

oss: oss.c oss.h virt_clock.o bit_vector.o
	$(CC) oss.c virt_clock.o bit_vector.o -o oss $(LDFLAGS)

user: user.c oss.h virt_clock.o
	$(CC)  user.c virt_clock.o -o user

virt_clock.o: virt_clock.c virt_clock.h
	$(CC) -c virt_clock.c

bit_vector.o: bit_vector.c bit_vector.h
	$(CC) -c bit_vector.c

clean:
	rm oss user oss.log *.o

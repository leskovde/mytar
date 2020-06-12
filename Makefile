CFLAGS = -I. -Wall -Wextra
TESTS = $(shell cat tests/phase-1.tests)

mytar: mytar.o
	$(CC) $(CFLAGS) -o $@ mytar.o

clean:
	rm mytar.o mytar

test:
	cd tests && ./run-tests.sh $(TESTS)

CFLAGS = -I. -Wall -Wextra

mytar: mytar.o
	$(CC) $(CFLAGS) -o $@ mytar.o

clean:
	rm mytar.o mytar

test:
	cd tests && ./run-tests.sh

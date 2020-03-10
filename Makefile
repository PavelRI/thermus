all: thermus

thermus: main.o
	$(CC)  -I/usr/include -L/usr/lib -o $@ $^ -lncursesw -lhidapi-libusb

%.o: %.c
	$(CC) $(CFLAGS) -c -I. -o $@ $^

clean:
	rm -f *.o thermus

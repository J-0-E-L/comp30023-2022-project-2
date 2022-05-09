CC = gcc
CFLAGS = -Wall
OUT = server
OBJ = main.o

server: $(OBJ)
	$(CC) $(CFLAGS) -o $(OUT) $(OBJ)

clean:
	rm -f $(OBJ) $(OUT)

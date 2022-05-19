CC = gcc
CFLAGS = -Wall
OUT = server
HEAD = sockwrap.h
OBJ = main.o sockwrap.o
LIB = -lpthread

server: $(OBJ) $(HEAD)
	$(CC) $(CFLAGS) -o $(OUT) $(OBJ) $(LIB)

clean:
	rm -f $(OBJ) $(OUT)

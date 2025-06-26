SRC = phx2raw.c
OBJECTS = phx2raw.o
TARGET = phx2raw


phx2raw:
	gcc ${SRC} -o ${TARGET} ${OBJECTS}

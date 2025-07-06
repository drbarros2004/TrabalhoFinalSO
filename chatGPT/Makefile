CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2
TARGET = TrabalhoBarbeiro

all: $(TARGET)

$(TARGET): TrabalhoBarbeiro.c
	$(CC) $(CFLAGS) -o $(TARGET) TrabalhoBarbeiro.c

clean:
	rm -f $(TARGET) *.o


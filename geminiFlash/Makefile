CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2
TARGET = TrabalhoBarbeiroGemini

all: $(TARGET)

$(TARGET): TrabalhoBarbeiroGemini.c
	$(CC) $(CFLAGS) -o $(TARGET) TrabalhoBarbeiroGemini.c

clean:
	rm -f $(TARGET) *.o


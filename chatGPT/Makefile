CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2
TARGET = TrabalhoBarbeiroChatgpt

all: $(TARGET)

$(TARGET): TrabalhoBarbeiroChatgpt.c
	$(CC) $(CFLAGS) -o $(TARGET) TrabalhoBarbeiroChatgpt.c

clean:
	rm -f $(TARGET) *.o


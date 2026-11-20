CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -O2 -Iinclude
TARGET  = minihttpd

SRC_DIR = src
INC_DIR = include

SRCS    = $(wildcard $(SRC_DIR)/*.c)
OBJS    = $(SRCS:.c=.o)
HEADERS = $(wildcard $(INC_DIR)/*.h)

.PHONY: all run debug clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS = -std=c11 -Wall -Wextra -pedantic -g -O0 -fsanitize=address,undefined -Iinclude
debug: clean $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
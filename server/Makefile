SRCS=aesdsocket.c
OBJS=$(SRCS:.c=.o)
TARGET=aesdsocket

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -Wextra -g

.PHONY:all
all: $(TARGET)

$(OBJS) : %.o : %.c 
	$(CC) $(CFLAGS) -c $^ -o $@ 

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $(TARGET) 

.PHONY:clean
clean: 
	rm -f $(OBJS) $(TARGET)
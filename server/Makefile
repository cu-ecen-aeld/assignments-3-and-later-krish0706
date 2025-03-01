SRCS=aesdsocket.c
OBJS=$(SRCS:.c=.o)

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -Wextra -g
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

.PHONY:all
all: $(TARGET)

$(OBJS) : %.o : %.c 
	$(CC) $(CFLAGS) -c $^ -o $@ $(INCLUDES) $(LDFLAGS) 

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $(TARGET) $(INCLUDES) $(LDFLAGS) 

.PHONY:clean
clean: 
	rm -f $(OBJS) $(TARGET)
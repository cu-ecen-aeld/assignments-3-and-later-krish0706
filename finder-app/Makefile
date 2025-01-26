CC := gcc
objects = writer

ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
	CC := $(CROSS_COMPILE)$(CC)
endif

.PHONY:all
all: $(objects)

$(objects): % : %.c 
	$(CC) $^ -o $@ 

.PHONY:clean
clean: 
	rm -f ./$(objects)
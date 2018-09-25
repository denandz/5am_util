CFLAGS = -Wall -g -O3
TARGET = 5am_util

SRCS = main.c util.c
OBJS = $(SRCS:.c=.o)

.PHONY: all

$(TARGET): $(OBJS)
	$(CC) ${LDFLAGS} -o $@ $^ ${LIBS}

$(SRCS:.c):%.c
	$(CC) $(CFLAGS) -MM $<

.PHONY: clean
clean:
	rm -f ${OBJS} ${TARGET}

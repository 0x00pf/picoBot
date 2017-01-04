all: picobot picobot2

CFLAGS=-g -O0

picobot: picobot.c
	${CC} -o $@ $<

picobot2: picobot2.c
	${CC} ${CFLAGS} -o $@ $<
.PHONY:
clean:
	rm -f picobot picobot2


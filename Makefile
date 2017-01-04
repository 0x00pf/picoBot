all: picobot picobot2 picobot4 

CFLAGS=-g -O0

picobot: picobot.c
	${CC} -o $@ $<

picobot2: picobot2.c
	${CC} ${CFLAGS} -o $@ $<

picobotxi32: picobot4.c
	${CC} ${CFLAGS} -o $@ $<
.PHONY:
clean:
	rm -f picobot picobot2 picobot4


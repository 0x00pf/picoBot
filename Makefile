all: picobot

picobot: picobot.c
	${CC} -o $@ $<

.PHONY:
clean:
	rm -f picobot

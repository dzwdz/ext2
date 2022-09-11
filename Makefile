.POSIX:
CFLAGS = -Wall -Wextra -Werror
OBJ := opendev.o read.o

libext2.a: ${OBJ}
	rm -f $@
	${AR} rc $@ ${OBJ}

example: example.o libext2.a

.PHONY: clean
clean:
	rm -f libext2.a ${OBJ} example example.o

${OBJ} example.o: ext2.h ext2d.h


empty.e2:
	 mkfs.ext2 $@ 1024

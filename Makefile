.POSIX:
CFLAGS = -Wall -Wextra -Werror
OBJ := opendev.o read.o write.o unlink.o req.o

libext2.a: ${OBJ}
	rm -f $@
	${AR} rc $@ ${OBJ}

example: example.o ex_cache.o libext2.a

.PHONY: clean
clean:
	rm -f libext2.a ${OBJ} example example.o ex_cache.o

${OBJ} example.o: ext2.h ext2d.h
example.o ex_cache.o: ex_cache.h


empty.e2:
	 mkfs.ext2 $@ 1024

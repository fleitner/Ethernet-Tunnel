
all:
	gcc -o sysrw.o -c sysrw.c
	gcc -lpthread -o ethertund sysrw.o ethertund.c

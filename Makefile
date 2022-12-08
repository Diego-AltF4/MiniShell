CC = gcc
WARNINGS = -Wall -Wextra -no-pie
LIB = ArchivosDados/libparser.a
msh: main.c
	$(CC) $? $(WARNINGS) $(LIB) -o $@ 
clean:
	rm msh
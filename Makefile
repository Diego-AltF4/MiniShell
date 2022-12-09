CC = gcc
WARNINGS = -Wall -Wextra -static
LIB = ArchivosDados/libparser.a
msh: main.c
	$(CC) $? $(WARNINGS) $(LIB) -o $@ 
clean:
	rm msh
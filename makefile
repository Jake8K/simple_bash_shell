C = smallsh.c

smallsh: smallsh.c
	gcc -g smallsh.c -o smallsh

clean: 
	rm smallsh

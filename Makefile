editor: editor.c
	gcc editor.c -o editor -Wall -Wextra -pedantic -std=c99

clean: 
	rm editor *.o a.out

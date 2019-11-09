Simulador: Simulador.o LinkedList.o
	gcc -Wall -pthread Simulador.o LinkedList.o -o Simulador -lrt

Simulador.o: Simulador.c LinkedList.h
	gcc -c Simulador.c LinkedList.h -pthread

LinkedList.o:	LinkedList.c
	gcc -c LinkedList.c -pthread
clean:
	$(RM) Simulador

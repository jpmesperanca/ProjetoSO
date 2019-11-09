Simulador: Simulador.o LinkedList.o
	gcc -Wall -pthread Simulador.o LinkedList.o -o Simulador -lrt

Simulador.o: Simulador.c LinkedList.h
	gcc -c Simulador.c LinkedList.h -pthread

LinkedList.o:	LinkedList.c LinkedList.h
	gcc -c LinkedList.c LinkedList.h -pthread
clean:
	$(RM) Simulador

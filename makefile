Simulador: Simulador.o LinkedList.h
	gcc -Wall -pthread Simulador.o LinkedList.o -o Simulador -lrt

Simulador.o: Simulador.c LinkedList.h
	gcc -c Simulador.c

clean:
	$(RM) Simulador

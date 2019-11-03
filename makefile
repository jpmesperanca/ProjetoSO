Simulador: Simulador.o
	gcc -Wall -pthread Simulador.o -o Simulador -lrt
Simulador.o: Simulador.c
	gcc -c Simulador.c -pthread

clean:
	$(RM) Simulador

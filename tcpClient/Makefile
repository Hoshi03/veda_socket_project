OBJS =  tcpClient.o
all :  tcpClient

tcpClient: tcpClient.o
	$(CC) -o $@ tcpClient.o
	$(CC) -c tcpClient.c

clean :
	rm *.o  tcpClient

# Makefile
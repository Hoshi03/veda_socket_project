CC = aarch64-linux-gnu-gcc
LDFLAGS = -L/usr/lib/aarch64-linux-gnu -lmariadb
TARGET = tcpServer
SRCS = tcpServer.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
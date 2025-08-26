CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LIBS = -lX11 -lutil
TARGET = dermux
SOURCE = dermo.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

.PHONY: clean install uninstall
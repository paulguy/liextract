OBJS   = riff.o liextract.o
TARGET = liextract
CFLAGS = -Wall -Wextra -D_FILE_OFFSET_BITS=64 -ggdb

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean

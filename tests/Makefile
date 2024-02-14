CC = clang

CFLAGS += -W -Wall -Wextra -O3 -g -I../ -DDEBUG -fno-omit-frame-pointer
CFLAGS += -DPL7M_USE_INTERNAL_FUZZER_MUTATE -DPL7M_USE_64K_PACKETS -DPL7M_ENABLE_ERROR #-DPL7M_ENABLE_LOG
CFLAGS += -fsanitize=address,undefined -fno-sanitize=alignment
#CFLAGS += -fsanitize=memory

LIBS=-lpcap

DEPS = ../pl7m.h Makefile
OBJ = ../pl7m.o pl7m_test.o

pl7m_test: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ core ../pl7m.o pl7m_test

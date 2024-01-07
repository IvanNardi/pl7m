CC = clang

CFLAGS += -W -Wall -Wextra -O0 -g -I../ -DDEBUG -fno-omit-frame-pointer
CFLAGS += -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION #-DPL7M_USE_INTERNAL_FUZZER_MUTATE -DPL7M_ENABLE_ERROR #-DPL7M_ENABLE_LOG
CFLAGS += -fsanitize=fuzzer
CFLAGS += -fsanitize=address,undefined  -fno-sanitize=alignment
#CFLAGS += -fsanitize=memory -fsanitize-memory-track-origins

LIBS=-lpcap

OBJ = ../pl7m.o fuzzer_example.o

fuzzer_example: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)


.PHONY: clean

clean:
	rm -f $(OBJ) *.o *~ core fuzzer_example

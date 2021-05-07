TARGETS=main

CC_C=gcc

all: clean $(TARGETS)

$(TARGETS):
	$(CC_C) $(CFLAGS) $@.c -o $@

clean:
	rm -f $(TARGETS) execvResult.txt

test: all
	./$(TARGETS) -n 6 -p 0.3 -q 4 -t 5 -b 0.05
	make clean
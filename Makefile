CC=gcc
FLAGS=-pthread --std=gnu99 -ggdb3 -Wall -lm
REGEN_TAGS=@echo "regenerating tags..." && ctags -R
REGEN_LIST=@echo "regenerating file list..." && ./listgen.sh

all: search-engine

search-engine: search-engine.o index.o
	@echo "linking..." && $(CC) $^ -o $@ $(FLAGS)
	$(REGEN_LIST)
	$(REGEN_TAGS)

search-engine.o: search-engine.c
	@echo "compiling search-engine.c..." && $(CC) -c $^ -o $@ $(FLAGS)

index.o: index.c
	@echo "compiling index.c..." && $(CC) -c $^ -o $@ $(FLAGS)

test: test.c index.o
	@echo "building test program..." && $(CC) $^ -o $@ $(FLAGS)

clean-obj:
	@echo "removing object files..." && rm -f *.o

clean: clean-obj
	@echo "removing binaries and file list..." && rm -f search-engine test files-list.txt


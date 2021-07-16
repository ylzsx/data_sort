DESTINATION = ./source_data_out.dat
CC = gcc
CFLAGS = -Wall
LDFLAGS = -pthread
RM =  ~/bash_tools/rm.sh

SORT = sort
OBJ = main.o data_sort.o mypipe.o

.PHONY: all clean

all: $(SORT)

clean:
	$(RM) $(SORT) $(OBJ) ./tmp/* $(DESTINATION)

$(SORT): $(OBJ)
	$(CC) $^ -g -o $@ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

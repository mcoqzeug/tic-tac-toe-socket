# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -std=gnu99

all:  tictactoeServer tictactoeClient

tictactoeServer: tictactoeServer.c tictactoe.h tictactoe.c server.c
	$(CC) $(CFLAGS) -o tictactoeServer tictactoeServer.c tictactoe.c server.c

tictactoeClient: tictactoeClient.c tictactoe.h tictactoe.c client.c
	$(CC) $(CFLAGS) -o tictactoeClient tictactoeClient.c tictactoe.c client.c

clean:
	$(RM) tictactoeServer tictactoeClient

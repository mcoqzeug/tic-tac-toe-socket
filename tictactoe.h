#ifndef TICTACTOE_H
#define TICTACTOE_H

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <memory.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "tictactoe.h"


#define ROWS  3
#define COLUMNS  3
#define MAX_BOARD 3
#define MAX_TRY 3

#define CLIENT_MARK 'X'
#define SERVER_MARK 'O'

// 1st byte
#define VERSION 8

// 3rd byte
#define GAME_ON 0
#define GAME_COMPLETE 1
#define GAME_ERROR 2
//#define RETRY 3

// 4th byte, when 3rd byte == GAME_COMPLETE
#define DRAW 1
#define WIN 2
#define LOSE 3

// 4th byte, when 3rd byte == GAME_ERROR
#define OUT_OF_RESOURCES 1
#define MALFORMED_REQUEST 2
#define SERVER_SHUTDOWN 3
#define TIME_OUT 4
#define TRY_AGAIN 5

// 5th Byte
#define NEW_GAME 0
#define MOVE 1
#define END_GAME 2
#define RECONNECT 3

#define BUFFER_SIZE 1000

#define TIME_LIMIT_SERVER 10

#define DELIM "."

#define MAX_SEND_COUNT 3

// multicast
#define MC_PORT 1818
#define MC_GROUP "239.0.0.1"


int parseGeneralError(uint8_t statusModifier);

int checkWin(char board[ROWS][COLUMNS], char mark);

int isMoveValid(char board[ROWS][COLUMNS], int row, int column, int choice);

void initBoard(char board[ROWS][COLUMNS]);

void printBoard(char board[ROWS][COLUMNS], char mark);

void playServer(
        int sd_stream,
        int sd_dgram,
        long portNumber);

void playClient(
        int connected_sd,
        int sd_dgram,
        struct sockaddr_in multicast_address);

int isIpValid(const char *ip_str);

int isPortNumValid(const char *portNum);

int sendBuffer(
        int connected_sd,
        uint8_t buffer[BUFFER_SIZE]);

int sendMoveWithChoice(
        int sd,
        uint8_t choice,
        uint8_t gameId,
        uint8_t sequenceNum,
        char board[ROWS][COLUMNS],
        char mark);

void respondToInvalidRequest(
        int sd,
        int sendSequenceNum,
        uint8_t gameId);

void u16_to_u8(uint16_t port_s, uint8_t port_array[2]);

uint16_t u8_to_u16(const uint8_t port_array[2]);

#endif

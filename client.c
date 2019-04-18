#include "tictactoe.h"


#define LOOP_CONTINUE 1
#define LOOP_BREAK 0

#define FILE_ROWS 10
#define FILE_LINE_LENGTH 100


static uint8_t bufferRecv[BUFFER_SIZE];
// static uint8_t bufferSend[BUFFER_SIZE];

char ipAddresses[FILE_ROWS][FILE_LINE_LENGTH];
uint16_t portNumbers[FILE_ROWS];


int buildGameForClient(int connected_sd, char board[ROWS][COLUMNS]);


/*
 * use scanf to get a move from client
 */
uint8_t clientMakeChoice(char board[ROWS][COLUMNS]) {
    uint8_t choice = 1;
    int valid = 0;

    while (valid == 0) {
        printf("Enter a number:  ");

        char tmp;
        scanf("%s", &tmp);
        choice = (uint8_t) strtol(&tmp, NULL, 10);

        int row = (choice-1) / ROWS;
        int column = (choice-1) % COLUMNS;
        valid = isMoveValid(board, row, column, choice);
    }
    return choice;
}

/*
 * return 1 if receive successfully; return 0 otherwise
 */
int recvBuffer(int sd) {
    memset(bufferRecv, 0, BUFFER_SIZE);
    int rc = read(sd, bufferRecv, BUFFER_SIZE);
    if (rc == 0) {
        printf("Server is disconnected.\n");
        return 0;
    } else if (rc < 0) {
        perror("Fail to read");
        return 0;
    } else if (rc < BUFFER_SIZE) {
        printf("Received only %d bytes. (should have received %d bytes)\n", rc, BUFFER_SIZE);
        return 0;
    }
    return 1;
}


/*
 *  return LOOP_CONTINUE or LOOP_BREAK
 */
int receiveMoveClient(int connected_sd, int sendSequenceNum, char board[ROWS][COLUMNS]) {
    const uint8_t recvStatus = bufferRecv[2];
    const uint8_t statusModifier = bufferRecv[3];
    const uint8_t gameId = bufferRecv[5];
    if (recvStatus < 0 || recvStatus > 2) {
        printf("Received invalid game status: %d.\n", recvStatus);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    if (recvStatus == GAME_ERROR) {
        parseGeneralError(statusModifier);
        return LOOP_BREAK;
    }

    // when recvStatus == GAME_ON or GAME_COMPLETE

    // check if move is valid
    uint8_t choice = bufferRecv[1];

    int row = (choice-1) / ROWS;
    int column = (choice-1) % COLUMNS;

    if (isMoveValid(board, row, column, choice) == 0) {
        printf("The opponent made an invalid move: %d.\n", choice);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }

    // move is valid, update board
    board[row][column] = SERVER_MARK;
    printBoard(board, CLIENT_MARK);

    // check local game finished
    int result = checkWin(board, SERVER_MARK);

    if (recvStatus == GAME_ON) {
        if (result == GAME_ON) {
            uint8_t newChoice = clientMakeChoice(board);
            sendMoveWithChoice(
                    connected_sd, newChoice, gameId,
                    (uint8_t) sendSequenceNum, board, CLIENT_MARK);
            return LOOP_CONTINUE;
        }
        printf("Received invalid game status: %d, expected: %d.\n", recvStatus, GAME_ON);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    // when recvStatus == GAME_COMPLETE
    // check if local game and remote game has the same result
    if (result != statusModifier) {
        printf("Received invalid status modifier: %d. Expected: %d\n", statusModifier, result);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    uint8_t sm;
    if (result == WIN) {
        printf("You lose.\n");
        sm = LOSE;
    } else {
        printf("Draw.\n");
        sm = DRAW;
    }
    uint8_t sb[BUFFER_SIZE] = {
            VERSION, 0, GAME_COMPLETE, sm, END_GAME, gameId,
            (uint8_t) sendSequenceNum};
    sendBuffer(connected_sd, sb);
    return LOOP_BREAK;
}


/*
 * Function: processBufferClient
 * -----------------------------
 *   Receive a move from the other node
 *
 *   connected_sd: socket file descriptor
 *
 *   resendCountPtr:
 *
 *   gameId:
 *
 *   sequenceNumPtr: points to the address
 *   of the last sent sequence number
 *
 *   board[ROWS][COLUMNS]: the board for the game
 *
 *   return LOOP_CONTINUE or LOOP_BREAK
 */
int processBufferClient(
        int connected_sd,
        // int *resendCountPtr,
        uint8_t gameId,
        uint8_t *sequenceNumPtr,
        char board[ROWS][COLUMNS]) {

    printf("RECEIVE choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           bufferRecv[1], bufferRecv[2], bufferRecv[3],
           bufferRecv[4], bufferRecv[5], bufferRecv[6]);

    const int recvSequenceNum = bufferRecv[6];
    const int expectedRecvSeqNum = (*sequenceNumPtr + 1) % 256;
    const int sendSequenceNum = (expectedRecvSeqNum + 1) % 256;

    uint8_t version = bufferRecv[0];
    if (version != VERSION) {
        printf("Received invalid version number: %d.\n", version);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }

    // #receivedBytes and #version are correct and no timeout
    const uint8_t gameType = bufferRecv[4];
    if (gameType < 1 || gameType > 2) {
        printf("Received invalid game type: %d.\n", gameType);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }

    // Below are the cases when gameType == END_GAME, MOVE
    // need to check gameId and seqNum
    if (gameId != bufferRecv[5]) {
        printf("Received invalid game id: %d expected: %d.\n", bufferRecv[5], gameId);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    // gameId is correct, check sequenceNum
    if (recvSequenceNum < expectedRecvSeqNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input
//        if (*resendCountPtr < MAX_TRY) {
//            printf("Received a duplicate packet, resend last msg.\n");
//            *resendCountPtr += 1;
//            sendBuffer(connected_sd, bufferSend);
//            return LOOP_CONTINUE;
//        }
        printf("Received a duplicate packet, run out of resend chances, exit game.\n");
        return LOOP_BREAK;
    }
    if (recvSequenceNum > expectedRecvSeqNum) {
        printf("Packets arrived out of order. Received sequence number: %d, expected: %d.\n",
               recvSequenceNum, expectedRecvSeqNum);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    // when gameId, seqNum are all correct,
    // gameType can be END_GAME or MOVE
    // update sequenceNumPtr
    *sequenceNumPtr = (uint8_t) sendSequenceNum;

    if (gameType == END_GAME) {
        int result = checkWin(board, SERVER_MARK);
        if (result == GAME_ON || result == WIN) {
            printf("Invalid END GAME command.\n");
            respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
            return LOOP_BREAK;
        }
        if (result == DRAW) printf("Draw.\n");
        else printf("You win!\n");
        return LOOP_BREAK;
    }
    // when gameType == MOVE
    return receiveMoveClient(connected_sd, sendSequenceNum, board);
}


/*
 * return -1 if there's an error, otherwise return gameId (should be a non-negative integer)
 */
int buildGameForClient(int connected_sd, char board[ROWS][COLUMNS]) {
    // send new game request
    uint8_t sb[BUFFER_SIZE] = {VERSION, 0, GAME_ON, 0, NEW_GAME, 0, 0};
    sendBuffer(connected_sd, sb);

    // receive response
    int recvResult = recvBuffer(connected_sd);

    printf("RECEIVE choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           bufferRecv[1], bufferRecv[2], bufferRecv[3],
           bufferRecv[4], bufferRecv[5], bufferRecv[6]);

    if (recvResult == 1) {
        uint8_t recvSequenceNum = bufferRecv[6];

        // check sequence number
        if (recvSequenceNum < 1) {
            // receiving a duplicate packet
            printf("Received a duplicate packet.\n");
            return -1;
        }
        if (recvSequenceNum > 1) {
            printf("Packets arrived out of order. Received sequence number: %d"
                   ", expected: %d.\n", recvSequenceNum, 1);
            return -1;
        }
        uint8_t recvStatus = bufferRecv[2];
        uint8_t statusModifier = bufferRecv[3];
        if (recvStatus == GAME_ERROR) {
            parseGeneralError(statusModifier);
        } else {
            // client send 1st move
            printBoard(board, CLIENT_MARK);
            uint8_t choice = clientMakeChoice(board);
            int sendMoveResult = sendMoveWithChoice(
                    connected_sd,
                    choice,
                    bufferRecv[5],
                    2,
                    board,
                    CLIENT_MARK);
            if (sendMoveResult == GAME_ON) return bufferRecv[5];
        }
    }
    printf("Cannot build Game with server.\n");
    return -1;
}


/*
 * sd_dgram:
 *
 * multicast_address: address of the multicast group
 *
 * return -1 if failed, otherwise return the sd_stream of a newly connected stream socket (should be non-negative)
 */
int multicast(int sd_dgram, struct sockaddr_in multicast_address) {
    printf("MULTICASTING\n");
    // send
    uint8_t bufferSend[BUFFER_SIZE];
    memset(bufferSend, 0, sizeof(bufferSend));
    bufferSend[0] = VERSION;
    bufferSend[1] = 1;

    int cnt = sendto(sd_dgram, bufferSend, sizeof(bufferSend), 0,
            (struct sockaddr *) &multicast_address, sizeof(multicast_address));
    if (cnt < 0) {
        perror("sendto");
        close(sd_dgram);
        return -1;
    }

    printf("SEND multicast: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           bufferSend[1], bufferSend[2], bufferSend[3],
           bufferSend[4], bufferSend[5], bufferSend[6]);

    // receive
    fd_set socketFDS;
    int maxSD = sd_dgram;
    struct timeval timeout;

    FD_ZERO(&socketFDS);
    FD_SET(sd_dgram, &socketFDS);

    timeout.tv_sec = TIME_LIMIT_SERVER;
    timeout.tv_usec = 0;

    // block until something arrives
    int selectResult = select(maxSD+1, &socketFDS, NULL, NULL, &timeout);

    if (selectResult < 0) {
        perror("Failed to select");
        return -1;
    }
    if (selectResult == 0) {
        printf("No message in the past %d seconds.\n", TIME_LIMIT_SERVER);
        return -1;
    }

    if (FD_ISSET(sd_dgram, &socketFDS)) {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);
        cnt = recvfrom(sd_dgram, bufferRecv, sizeof(bufferRecv), 0, (struct sockaddr *) &addr, &addrLen);
        if (cnt < 0) {
            perror("Fail to read");
            return -1;
        } else if (cnt < BUFFER_SIZE) {
            printf("Received only %d bytes. (should have received %d bytes)\n", cnt, BUFFER_SIZE);
            return -1;
        }

        printf("RECEIVE multicast: %d status: %d statusModifier: %d "
               "gameType: %d gameId: %d sequenceNum: %d\n",
               bufferRecv[1], bufferRecv[2], bufferRecv[3],
               bufferRecv[4], bufferRecv[5], bufferRecv[6]);

        // check version
        if (bufferRecv[0] != VERSION) {
            printf("Received invalid multicast version number: %d, expected: %d.\n", bufferRecv[0], VERSION);
            return -1;
        }

        // check command
        if (bufferRecv[1] != 2) {
            printf("Received invalid multicast command: %d, expected: %d.\n", bufferRecv[1], 2);
            return -1;
        }

        // get port number
        uint8_t port_array[2] = {bufferRecv[2], bufferRecv[3]};
        uint16_t serverPortNumber = u8_to_u16(port_array);

        // connect
        // start stream socket
        int sd_stream = socket(AF_INET, SOCK_STREAM, 0);
        if(sd_stream < 0) {
            perror("Opening stream socket error");
            return -1;
        }

        struct sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_port = serverPortNumber;
        server_address.sin_addr = addr.sin_addr;

        if (connect(sd_stream, (struct sockaddr *) &server_address, sizeof(struct sockaddr_in)) < 0) {
            close(sd_stream);
            perror("connect error");
            return -1;
        }
        return sd_stream;
    }
    return -1;
}


/*
 * return gameId if success, otherwise return -1
 */
int reconnect(int connected_sd, char board[ROWS][COLUMNS]) {
    printf("RECONNECTING\n");

    // send
    uint8_t bufferSend[BUFFER_SIZE];
    memset(bufferSend, 0, sizeof(bufferSend));
    bufferSend[0] = VERSION;
    bufferSend[4] = RECONNECT;

    int boardIdx = 7;

    for (int i=0; i<ROWS; i++) {
        for (int j=0; j<COLUMNS; j++) {
            if (board[i][j] == SERVER_MARK)
                bufferSend[boardIdx] = 2;
            else if (board[i][j] == CLIENT_MARK)
                bufferSend[boardIdx] = 1;
            else
                bufferSend[boardIdx] = 0;
            boardIdx++;
        }
    }

    sendBuffer(connected_sd, bufferSend);

    // receive
    int recvResult = recvBuffer(connected_sd);
    if (recvResult == 0) {
        return -1;
    }

    printf("RECEIVE reconnect: %d status: %d statusModifier: %d "
               "gameType: %d gameId: %d sequenceNum: %d\n",
               bufferRecv[1], bufferRecv[2], bufferRecv[3],
               bufferRecv[4], bufferRecv[5], bufferRecv[6]);

    // Server responds with the game number in addition to their move,
    // or with a reconnect error if they became full.
    int recvMoveResult = receiveMoveClient(connected_sd, 0, board);
    if (recvMoveResult == LOOP_BREAK) {
        close(connected_sd);
        return -1;
    }
    return bufferRecv[5];
}


/*
 * return sd_stream if succeed, otherwise return -1
 */
int connectToServer() {
    printf("Accessing config file.\n");
    for (int i=0; i<FILE_ROWS; i++) {
        int sd_stream = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_address;

        if(sd_stream < 0) {
            perror("Opening stream socket error");
            continue;
        }

        server_address.sin_family = AF_INET;
        server_address.sin_port = portNumbers[i];
        server_address.sin_addr.s_addr = inet_addr(ipAddresses[i]);

        if (connect(sd_stream, (struct sockaddr *) &server_address, sizeof(struct sockaddr_in)) < 0) {
            close(sd_stream);
            perror("connect error");
            continue;
        }
        return sd_stream;
    }
    return -1;
}


void playClient(
        int connected_sd,
        int sd_dgram,
        struct sockaddr_in multicast_address) {

    FILE * fp;
    char *line = NULL;
    size_t len = 0;

    fp = fopen("ip_addresses", "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

    int i = 0;
    while (getline(&line, &len, fp) != -1) {
        if (i >= FILE_ROWS) {
            printf("WARNING: Input file too long.\n");
            break;
        }

        strcpy(ipAddresses[i], line);

        char portStr[FILE_LINE_LENGTH];
        int k = 0;

        int ipLength = FILE_LINE_LENGTH;
        for (int j=0; j<FILE_LINE_LENGTH; j++) {
            if (ipAddresses[i][j] == ' ') {
                ipLength = j;
                continue;
            }

            if (j > ipLength) {  // port number
                portStr[k] = ipAddresses[i][j];
                k++;
            }
        }

        portNumbers[i] = htons(strtol(portStr, NULL, 10));

        ipAddresses[i][ipLength] = 0;

        i++;
    }

    fclose(fp);

    char board[ROWS][COLUMNS];
    initBoard(board);

    uint8_t gameId, sequenceNum;

    gameId = buildGameForClient(connected_sd, board);
    if (gameId < 0) {
        connected_sd = multicast(sd_dgram, multicast_address);
        if (connected_sd < 0) {
            connected_sd = connectToServer();
            if (connected_sd < 0) {
                return;
            }
        }
        gameId = reconnect(connected_sd, board);
        if (gameId < 0) {
            return;
        }
        sequenceNum = 0;
    } else {
        sequenceNum = 2;
    }

    for (;;) {
        int recvResult = recvBuffer(connected_sd);
        if (recvResult == 0) {
            close(connected_sd);
            connected_sd = multicast(sd_dgram, multicast_address);
            if (connected_sd < 0) {
                connected_sd = connectToServer();
                if (connected_sd < 0) {
                    return;
                }
            }
            gameId = reconnect(connected_sd, board);
            if (gameId < 0) {
                return;
            }
            sequenceNum = 0;
            continue;
        }
        int processResult = processBufferClient(connected_sd, gameId, &sequenceNum, board);
        if (processResult == LOOP_BREAK) return;
    }
}

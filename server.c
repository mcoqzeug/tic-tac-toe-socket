#include "tictactoe.h"


struct board_info {
	int resendCount;
	int sd;
	time_t latest_time;
	uint8_t sequenceNum;  // store the expected sequence number sent by the client
	uint8_t bufferSend[BUFFER_SIZE];
} boardInfo[MAX_BOARD+1];


void initBoardInfo(struct board_info *boardInfoPtr) {
    boardInfoPtr->resendCount = 0;
    boardInfoPtr->sd = 0;
    time(&boardInfoPtr->latest_time);
    boardInfoPtr->sequenceNum = 0;
    memset(boardInfoPtr->bufferSend, 0, BUFFER_SIZE);
}


uint8_t serverMakeChoice(char board[ROWS][COLUMNS]) {
    int choice = 0;
    for (int i=0; i<ROWS; i++) {
        for (int j=0; j<COLUMNS; j++) {
            choice = i*3+j+1;
            if (board[i][j] == (char) (choice + '0'))
                return (uint8_t) choice;
        }
    }
    return (uint8_t) choice;
}


void receiveNewGame(
        int recvSequenceNum,
        int sendSequenceNum,
        int nextRecvSequenceNum,
        uint8_t gameId) {

    // check sequence number
    if (recvSequenceNum < boardInfo[gameId].sequenceNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input
        if (boardInfo[gameId].resendCount < MAX_TRY) {
            printf("Received a duplicate packet, resend last msg.\n");
            boardInfo[gameId].resendCount++;
            sendBuffer(boardInfo[gameId].sd, boardInfo[gameId].bufferSend);
            time(&boardInfo[gameId].latest_time);
        } else
            printf("Received a duplicate packet, run out of resend chances, exit game.\n");
        return;
    }
    if (recvSequenceNum > boardInfo[gameId].sequenceNum) {
        printf("Packets arrived out of order. "
               "Received sequence number: %d, expected: %d.\n",
               recvSequenceNum, boardInfo[gameId].sequenceNum);

        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    // update boardInfo
    boardInfo[gameId].sequenceNum = (uint8_t) nextRecvSequenceNum;
    time(&boardInfo[gameId].latest_time);

    // send game id to client
    uint8_t sb[BUFFER_SIZE] = {
            VERSION, 0, GAME_ON, 0, MOVE, gameId,(uint8_t) sendSequenceNum};
    sendBuffer(boardInfo[gameId].sd, sb);
}

void receiveReconnect(
        int gameId,
        int sendSequenceNum,
        const uint8_t buffer[BUFFER_SIZE],
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    printf("RECONNECT\n");

    initBoard(boards[gameId]);
    int boardIdx = 7;

    for (int i=0; i<ROWS; i++) {
        for (int j=0; j<COLUMNS; j++) {
            if (buffer[boardIdx] == 2) {
                boards[gameId][i][j] = SERVER_MARK;
            }
            else if (buffer[boardIdx] == 1) {
                boards[gameId][i][j] = CLIENT_MARK;
            }
            boardIdx++;
        }
    }

    printBoard(boards[gameId], SERVER_MARK);
    int result = checkWin(boards[gameId], CLIENT_MARK);
    if (result == GAME_ON) {
        time(&boardInfo[gameId].latest_time);
        uint8_t newChoice = serverMakeChoice(boards[gameId]);
        sendMoveWithChoice(
                boardInfo[gameId].sd, newChoice, gameId,
                (uint8_t) sendSequenceNum, boards[gameId], SERVER_MARK);
        return;
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
    sendBuffer(boardInfo[gameId].sd, sb);

    printf("Clean board %d after game completed.\n", gameId);
    close(boardInfo[gameId].sd);
    initBoard(boards[gameId]);
    initBoardInfo(&boardInfo[gameId]);
}


void receiveMove(
        int sendSequenceNum,
        const uint8_t buffer[BUFFER_SIZE],
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    const uint8_t recvStatus = buffer[2];
    const uint8_t statusModifier = buffer[3];
    const uint8_t gameId = buffer[5];
    if (recvStatus < 0 || recvStatus > 2) {
        printf("Received invalid game status: %d.\n", recvStatus);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    if (recvStatus == GAME_ERROR) {
        parseGeneralError(statusModifier);
        return;
    }

    // when recvStatus == GAME_ON or GAME_COMPLETE

    // check if move is valid
    uint8_t choice = buffer[1];

    int row = (choice-1) / ROWS;
    int column = (choice-1) % COLUMNS;

    if (isMoveValid(boards[gameId], row, column, choice) == 0) {
        printf("The opponent made an invalid move: %d.\n", choice);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }

    // move is valid, update board
    boards[gameId][row][column] = CLIENT_MARK;
    printBoard(boards[gameId], SERVER_MARK);

    // check local game finished
    int result = checkWin(boards[gameId], CLIENT_MARK);

    if (recvStatus == GAME_ON) {
        if (result == GAME_ON) {
            time(&boardInfo[gameId].latest_time);
            uint8_t newChoice = serverMakeChoice(boards[gameId]);
            sendMoveWithChoice(
                    boardInfo[gameId].sd, newChoice, gameId,
                    (uint8_t) sendSequenceNum, boards[gameId], SERVER_MARK);
            return;
        }
        printf("Received invalid game status: %d, expected: %d.\n", recvStatus, GAME_ON);
        respondToInvalidRequest(
                boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    // when recvStatus == GAME_COMPLETE
    // check if local game and remote game has the same result
    if (result != statusModifier) {
        printf("Received invalid status modifier: %d. Expected: %d\n", statusModifier, result);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
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
    sendBuffer(boardInfo[gameId].sd, sb);

    printf("Clean board %d after game completed.\n", gameId);
    close(boardInfo[gameId].sd);
    initBoard(boards[gameId]);
    initBoardInfo(&boardInfo[gameId]);
}


/*
 * Function: processBuffer
 * ----------------------------
 *   process a received buffer
 *
 *   gameId:
 *
 *   buffer:
 *
 *   board[ROWS][COLUMNS]: the board for the game
 */
void processBuffer(
        uint8_t gameId,
        const uint8_t buffer[BUFFER_SIZE],
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    printf("RECEIVE choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           buffer[1], buffer[2], buffer[3],
           buffer[4], buffer[5], buffer[6]);

    const int recvSequenceNum = buffer[6];
    const int sendSequenceNum = (recvSequenceNum + 1) % 256;
    const int nextRecvSequenceNum = (sendSequenceNum + 1) % 256;

    uint8_t version = buffer[0];
    if (version != VERSION) {
        printf("Received invalid version number: %d.\n", version);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    // #receivedBytes and #version are correct and no timeout
    const uint8_t gameType = buffer[4];
    if (gameType < 0 || gameType > 3) {
        printf("Received invalid game type: %d.\n", gameType);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    if (gameType == NEW_GAME) {
        receiveNewGame(recvSequenceNum, sendSequenceNum, nextRecvSequenceNum, gameId);
        return;
    }

    if (gameType == RECONNECT) {
        receiveReconnect(gameId, sendSequenceNum, buffer, boards);
        return;
    }

    // Below are the cases when gameType == END_GAME, MOVE
    // need to check gameId, port & ip, and seqNum
    if (gameId != buffer[5]) {
        printf("Received invalid game id: %d.\n", gameId);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    // gameId is correct, check sequenceNum
    if (recvSequenceNum < boardInfo[gameId].sequenceNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input
        if (boardInfo[gameId].resendCount < MAX_TRY) {
            printf("Received a duplicate packet, resend last msg.\n");
            boardInfo[gameId].resendCount++;
            sendBuffer(boardInfo[gameId].sd, boardInfo[gameId].bufferSend);
            time(&boardInfo[gameId].latest_time);
            return;
        }
        printf("Received a duplicate packet, run out of resend chances, exit game.\n");
        return;
    }
    if (recvSequenceNum > boardInfo[gameId].sequenceNum) {
        printf("Packets arrived out of order. Received sequence number: %d, expected: %d.\n",
                recvSequenceNum, boardInfo[gameId].sequenceNum);
        respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
        time(&boardInfo[gameId].latest_time);
        return;
    }
    // when gameId, seqNum are all correct,
    // gameType can be END_GAME or MOVE
    // update next expected received sequence number
    boardInfo[gameId].sequenceNum = (uint8_t) nextRecvSequenceNum;

    if (gameType == END_GAME) {
        time(&boardInfo[gameId].latest_time);

        int result = checkWin(boards[gameId], CLIENT_MARK);
        if (result == GAME_ON || result == WIN) {
            printf("Invalid END GAME command.\n");
            respondToInvalidRequest(boardInfo[gameId].sd, sendSequenceNum, gameId);
            time(&boardInfo[gameId].latest_time);
            return;
        }
        if (result == DRAW) printf("Draw.\n");
        else printf("You win!\n");

        close(boardInfo[gameId].sd);
        initBoard(boards[gameId]);
        initBoardInfo(&boardInfo[gameId]);
        return;
    }

    // when gameType == MOVE
    receiveMove(sendSequenceNum, buffer, boards);
}


void checkBoardTimeOut(char boards[MAX_BOARD][ROWS][COLUMNS]) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (boardInfo[i].sd != 0
            && time(NULL) - boardInfo[i].latest_time >= TIME_LIMIT_SERVER) {
            // this board is unavailable and has waited for too long
            if (boardInfo[i].resendCount < MAX_SEND_COUNT) {  // the server can still resend
                printf("Board[%d] timeout.\n", i);
                boardInfo[i].resendCount++;
                // sendBuffer(boardInfo[i].sd, boardInfo[i].bufferSend);
            } else {  // the server can't resend any more
                // tell the client its game has ended due to time out
                uint8_t sb[BUFFER_SIZE] = {
                        VERSION, 0, GAME_ERROR, TIME_OUT, MOVE, (uint8_t) i,
                        (uint8_t) (boardInfo[i].sequenceNum - 1) % 256};
                sendBuffer(boardInfo[i].sd, sb);

                printf("Clean board[%d] after time out.\n", i);
                close(boardInfo[i].sd);
                initBoard(boards[i]);
                initBoardInfo(&boardInfo[i]);
            }
        }
    }
}


void processMulticast(int sd_dgram, long portNumber) {
    printf("MULTICAST\n");

    uint8_t bufferSend[BUFFER_SIZE];
    uint8_t bufferRecv[BUFFER_SIZE];

    bufferSend[0] = VERSION;
    bufferSend[1] = 2;

    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    int cnt = recvfrom(sd_dgram, bufferRecv, sizeof(bufferRecv), 0, (struct sockaddr *) &addr, &addrLen);

    printf("RECEIVE choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           bufferRecv[1], bufferRecv[2], bufferRecv[3],
           bufferRecv[4], bufferRecv[5], bufferRecv[6]);

    int found = 0;
    for (int i = 0; i < MAX_BOARD; i++) {
        if (boardInfo[i].sd == 0) {
            found = 1;
            break;
        }
    }

    if (found == 0) {
        printf("There's no empty board for a multicast.\n");
        return;
    }

    if (cnt < 0) {
        perror("Fail to read");
    } else if (cnt < BUFFER_SIZE) {
        printf("Received only %d bytes. (should have received %d bytes)\n", cnt, BUFFER_SIZE);
    }

    // check version
    if (bufferRecv[0] != VERSION) {
        printf("Received invalid version number: %d, expected: %d.\n", bufferRecv[0], VERSION);
    }

    // check command
    if (bufferRecv[1] != 1) {
        printf("Received invalid version number: %d, expected: %d.\n", bufferRecv[1], 1);
    }

    uint8_t port_array[2];
    u16_to_u8(htons(portNumber), port_array);
    bufferSend[2] = port_array[0];
    bufferSend[3] = port_array[1];

    cnt = sendto(sd_dgram, bufferSend, sizeof(bufferSend), 0, (struct sockaddr *) &addr, sizeof(addr));

    printf("SEND choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           bufferSend[1], bufferSend[2], bufferSend[3],
           bufferSend[4], bufferSend[5], bufferSend[6]);

    if (cnt < 0) {
        perror("sendto in processMulticast");
        close(sd_dgram);
    }
}

/*
 * Function: playServer
 * ----------------------------
 *   Simulate the game play process for server
 *
 *   sd_stream: socket file descriptor of the server
 *
 *   sd_dgram:
 *
 *   multicast_address:
 */
void playServer(
        int sd_stream,
        int sd_dgram,
        long portNumber) {

    char boards[MAX_BOARD][ROWS][COLUMNS];

    // initialize boardInfo and boards
    for (int i=0; i<MAX_BOARD; ++i) {
        initBoardInfo(&boardInfo[i]);
        initBoard(boards[i]);
    }
    printBoard(boards[0], SERVER_MARK);

    // start the game
    for (long j=0; j<LONG_MAX; j++) {
        checkBoardTimeOut(boards);

        fd_set socketFDS;
        int maxSD = sd_stream;
        struct timeval timeout;

        FD_ZERO(&socketFDS);
        FD_SET(sd_stream, &socketFDS);
        FD_SET(sd_dgram, &socketFDS);

        if (sd_dgram> maxSD)
            maxSD = sd_dgram;

        // update socketFDS
        for (int i=0; i<MAX_BOARD; i++) {
            if (boardInfo[i].sd > 0) {
                FD_SET(boardInfo[i].sd, &socketFDS);
                if (boardInfo[i].sd > maxSD)
                    maxSD = boardInfo[i].sd;
            }
        }
        timeout.tv_sec = TIME_LIMIT_SERVER;
        timeout.tv_usec = 0;

        //printf("maxSD: %d, sd_dgram: %d, sd_stream: %d\n", maxSD, sd_dgram, sd_stream);

        // block until something arrives
        int selectResult = select(maxSD+1, &socketFDS, NULL, NULL, &timeout);

        if (selectResult < 0) {
            perror("Failed to select: ");
            //continue; todo
            break;
        }
        if (selectResult == 0) {
            printf("No message in the past %d seconds.\n", TIME_LIMIT_SERVER);
            continue;
        }

        if (FD_ISSET(sd_dgram, &socketFDS)) {
            processMulticast(sd_dgram, portNumber);
        }
        
        // establish new connection
        if (FD_ISSET(sd_stream, &socketFDS)) {
            uint8_t gameId;
            struct sockaddr_in from_address;
            socklen_t fromLength;
            int connected_sd = accept(sd_stream, (struct sockaddr *) &from_address, &fromLength);
            if (connected_sd < 0) perror("");

            for (gameId=0; gameId<MAX_BOARD; gameId++) {
                if (boardInfo[gameId].sd == 0) {
                    boardInfo[gameId].sd = connected_sd;
                    FD_SET(connected_sd, &socketFDS);
                    break;
                }
            }
            if (gameId == MAX_BOARD) {
                uint8_t sb[BUFFER_SIZE] = {
                        VERSION, 0, GAME_ERROR, OUT_OF_RESOURCES, MOVE, (uint8_t) 0, (uint8_t) 1};
                sendBuffer(connected_sd, sb);
                close(connected_sd);
            }
        }
        // receive buffer from all connected clients
        for (int i=0; i<MAX_BOARD; i++) {
            if (FD_ISSET(boardInfo[i].sd, &socketFDS)) {  // todo why not: if(boardInfo[gameId].sd_stream != 0)
                uint8_t buffer[BUFFER_SIZE];
                int rc = read(boardInfo[i].sd, &buffer, sizeof(buffer));
                if (rc == 0) { // the client disconnected normally
                    printf("Clean board %d after disconnected from client.\n", i);
                    close(boardInfo[i].sd); // close the socket
                    initBoardInfo(&boardInfo[i]);
                    initBoard(boards[i]);
                    continue;
                }
                if (rc < 0) {
                    perror("Fail to read: ");
                    continue;
                }
                if (rc < BUFFER_SIZE) {
                    printf("Received only %d bytes. (should have received %d bytes)\n", rc, BUFFER_SIZE);
                    continue;
                }
                processBuffer((uint8_t) i, buffer, boards);
            }
        }
    }
}

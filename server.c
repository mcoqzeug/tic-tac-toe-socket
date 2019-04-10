#include "tictactoe.h"


struct board_info {
	int resendCount;
	int sd;
	time_t latest_time;
	uint8_t sequenceNum;  // store the expected sequence number sent by the client
	uint8_t sendBuffer[BUFFER_SIZE];
};

static struct board_info boardInfo[MAX_BOARD+1];


void initBoardInfo(struct board_info *boardInfoPtr) {
    boardInfoPtr->resendCount = 0;
    boardInfoPtr->sd = 0;
    time(&boardInfoPtr->latest_time);
    boardInfoPtr->sequenceNum = 0;
    memset(boardInfoPtr->sendBuffer, 0, BUFFER_SIZE);
}


/*
 * automatically select a move for server
 */
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


/*
 *
 */
void respondToInvalidRequestServer(
        int sendSequenceNum,
        uint8_t gameId) {
    uint8_t sb[BUFFER_SIZE] = {
            VERSION, 0, GAME_ERROR, MALFORMED_REQUEST, MOVE, gameId,
            (uint8_t) sendSequenceNum};

    sendChoice(boardInfo[gameId].sd, sb);

    time(&boardInfo[gameId].latest_time);
}


/*
 * return either GAME_ERROR (2), SKIP_NEXT_SEND (6)
 */
int receiveNewGame(
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
            int sendResult = sendChoice(boardInfo[gameId].sd, boardInfo[gameId].sendBuffer);
            if (sendResult == 0) return GAME_ERROR;
            time(&boardInfo[gameId].latest_time);
            return SKIP_NEXT_SEND;
        } else {
            printf("Received a duplicate packet, run out of resend chances, exit game.\n");
            return GAME_ERROR;
        }
    }

    if (recvSequenceNum > boardInfo[gameId].sequenceNum) {
        printf("Packets arrived out of order. "
               "Received sequence number: %d, expected: %d.\n",
               recvSequenceNum, boardInfo[gameId].sequenceNum);

        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return SKIP_NEXT_SEND;
    }

    else {
        // update boardInfo
        boardInfo[gameId].sequenceNum = (uint8_t) nextRecvSequenceNum;
        time(&boardInfo[gameId].latest_time);

        // send game id to client
        uint8_t sb[BUFFER_SIZE] = {
                VERSION, 0, GAME_ON, 0, MOVE, gameId,
                (uint8_t) sendSequenceNum};

        int sendResult = sendChoice(boardInfo[gameId].sd, sb);

        if (sendResult == 0) return GAME_ERROR;

        return SKIP_NEXT_SEND;  // new game
    }
}


/*
 * return SKIP_NEXT_SEND(6), GAME_ERROR (2), GAME_ON (0)
 */
int receiveMove(
        int sendSequenceNum,
        const uint8_t buffer[BUFFER_SIZE],
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    const uint8_t recvStatus = buffer[2];
    const uint8_t statusModifier = buffer[3];
    const uint8_t gameId = buffer[5];

    if (recvStatus < 0 || recvStatus > 2) {
        printf("Received invalid game status: %d.\n", recvStatus);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return SKIP_NEXT_SEND;
    }

    if (recvStatus == GAME_ERROR) {
        parseGeneralError(statusModifier);
        return GAME_ERROR;
    }

    // when recvStatus == GAME_ON or GAME_COMPLETE

    // check if move is valid
    uint8_t choice = buffer[1];

    int row = (choice-1) / ROWS;
    int column = (choice-1) % COLUMNS;

    if (isMoveValid(boards[gameId], row, column, choice) == 0) {
        printf("The opponent made an invalid move: %d.\n", choice);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return SKIP_NEXT_SEND;
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
            sendMoveWithChoice(boardInfo[gameId].sd, newChoice, gameId, (uint8_t) sendSequenceNum, boards[gameId], SERVER_MARK);
            return GAME_ON;
        }
        printf("Received invalid game status: %d, expected: %d.\n", recvStatus, GAME_ON);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return SKIP_NEXT_SEND;
    }

    // when recvStatus == GAME_COMPLETE

    // check if local game and remote game has the same result
    if (result != statusModifier) {
        printf("Received invalid status modifier: %d. Expected: %d\n", statusModifier, result);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return SKIP_NEXT_SEND;
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
    int sendResult = sendChoice(boardInfo[gameId].sd, sb);

    time(&boardInfo[gameId].latest_time);

    if (sendResult == 0) return GAME_ERROR;

    close(boardInfo[gameId].sd);
    initBoard(boards[gameId]);
    initBoardInfo(&boardInfo[gameId]);

    return SKIP_NEXT_SEND;
}


/*
 * Function: processBuffer
 * ----------------------------
 *   process a received buffer
 *
 *   connected_sd: socket file descriptor
 *
 *   buffer:
 *
 *   board[ROWS][COLUMNS]: the board for the game
 *
 *   return: 0 outer loop should break, 1 outer loop should continue
 */
int processBuffer(
        uint8_t gameId,
        const uint8_t *buffer,
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    printf("RECEIVE choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           buffer[1], buffer[2], buffer[3],
           buffer[4], buffer[5], buffer[6]);

    uint8_t version = buffer[0];
    if (version != VERSION) {
        printf("Received invalid version number: %d.\n", version);
        respondToInvalidRequestServer(buffer[6], gameId);
        return 1;
    }

    const int recvSequenceNum = buffer[6];
    const int sendSequenceNum = (recvSequenceNum + 1) % 256;
    const int nextRecvSequenceNum = (sendSequenceNum + 1) % 256;

    // #receivedBytes and #version are correct and no timeout
    const uint8_t gameType = buffer[4];

    if (gameType < 0 || gameType > 2) {
        printf("Received invalid game type: %d.\n", gameType);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return 1;
    }

    if (gameType == NEW_GAME) {
        return receiveNewGame(recvSequenceNum, sendSequenceNum, nextRecvSequenceNum, gameId);
    }

    // Below are the cases when gameType == END_GAME, MOVE
    // need to check gameId, port & ip, and seqNum
    if (gameId != buffer[5]) {
        printf("Received invalid game id: %d.\n", gameId);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return 1;
    }

    // gameId is correct, check sequenceNum
    if (recvSequenceNum < boardInfo[gameId].sequenceNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input
        if (boardInfo[gameId].resendCount < MAX_TRY) {
            printf("Received a duplicate packet, resend last msg.\n");
            sendChoice(boardInfo[gameId].sd, boardInfo[gameId].sendBuffer);
            time(&boardInfo[gameId].latest_time);
            return 1;
        }
        printf("Received a duplicate packet, run out of resend chances, exit game.\n");
        return GAME_ERROR;
    }

    if (recvSequenceNum > boardInfo[gameId].sequenceNum) {
        printf("Packets arrived out of order. "
            "Received sequence number: %d, expected: %d.\n",
            recvSequenceNum, boardInfo[gameId].sequenceNum);
        respondToInvalidRequestServer(sendSequenceNum, gameId);
        return 1;
    }

    // when gameId, port & ip, seqNum are all correct,
    // gameType can be END_GAME or MOVE

    // update next expected received sequence number
    boardInfo[gameId].sequenceNum = (uint8_t) nextRecvSequenceNum;

    if (gameType == END_GAME) {
        time(&boardInfo[gameId].latest_time);

        int result = checkWin(boards[gameId], CLIENT_MARK);

        if (result == GAME_ON || result == WIN) {
            printf("Invalid END GAME command.\n");
            respondToInvalidRequestServer(sendSequenceNum, gameId);
            return 1;
        }

        if (result == DRAW) printf("Draw.\n");
        else printf("You win!\n");

        close(boardInfo[gameId].sd);
        initBoard(boards[gameId]);
        initBoardInfo(&boardInfo[gameId]);

        return 1;
    }

    // when gameType == MOVE
    return receiveMove(sendSequenceNum, buffer, boards);
}


/*
 * todo
 */
void checkBoardTimeOut(
        int sd,
        char boards[MAX_BOARD][ROWS][COLUMNS]) {

    for (int i = 0; i < MAX_BOARD; i++) {
        if (boardInfo[i].sd != 0
            && time(NULL) - boardInfo[i].latest_time >= TIME_LIMIT_SERVER) {
            // this board is unavailable and has waited for too long

            if (boardInfo[i].resendCount <= MAX_SEND_COUNT) {
                // the server can still resend
                boardInfo[i].resendCount++;

                printf("RESEND when timeout.\n");
                sendChoice(sd, boardInfo[i].sendBuffer);

            } else {  // the server can't resend any more
                int sendSequenceNum = (boardInfo[i].sequenceNum - 1) % 256;

                close(boardInfo[i].sd);
                initBoard(boards[i]);
                initBoardInfo(&boardInfo[i]);
                printf("Board[%d] time out.\n", i);

                uint8_t sb[BUFFER_SIZE] = {
                        VERSION, 0, GAME_ERROR, TIME_OUT, MOVE, (uint8_t) i,
                        (uint8_t) sendSequenceNum};

                // tell the client its game has ended due to time out
                sendChoice(sd, sb);
            }
        }
    }
}


/*
 * Function: playServer
 * ----------------------------
 *   Simulate the game play process for server
 *
 *   sd: socket file descriptor
 *
 *   opponent_address_pointer: the pointer pointing to a
 *   sockaddr structure containing the opponent's address
 */
void playServer(int sd) {
    char boards[MAX_BOARD][ROWS][COLUMNS];

    // initialize boardInfo and boards
    for (int i=0; i<MAX_BOARD; ++i) {
        initBoardInfo(&boardInfo[i]);
        initBoard(boards[i]);
    }

    printBoard(boards[0], SERVER_MARK);

    // start the game
    for (long j=0; j<LONG_MAX; j++) {
        checkBoardTimeOut(sd, boards);

        fd_set socketFDS;
        int maxSD = sd;
        struct timeval timeout;

        FD_ZERO(&socketFDS);
        FD_SET(sd, &socketFDS);

        // update socketFDS
        for (int i=0; i<MAX_BOARD; i++) {
            if (boardInfo[i].sd > 0) {
                FD_SET(boardInfo[i].sd, &socketFDS);
                if (boardInfo[i].sd > maxSD) {
                    maxSD = boardInfo[i].sd;
                }
            }
        }

        timeout.tv_sec = TIME_LIMIT_SERVER;
        timeout.tv_usec = 0;

        // block until something arrives
        int selectResult = select(maxSD+1, &socketFDS, NULL, NULL, &timeout);

        if (selectResult < 0) {
            perror("Failed to select: ");
            break;
        }

        if (selectResult == 0) {
            printf("No message in the past %d seconds.\n", TIME_LIMIT_SERVER);
            continue;
        }

        uint8_t gameId;
        int connected_sd;
        struct sockaddr_in from_address;
        socklen_t fromLength;

        // establish new connection
        if (FD_ISSET(sd, &socketFDS)) {  // todo why?
            connected_sd = accept(sd, (struct sockaddr *) &from_address, &fromLength);
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
                sendChoice(connected_sd, sb);
                close(connected_sd);
            }
        }

        // receive buffer from all connected clients
        for (int i=0; i<MAX_BOARD; i++) {
            if (FD_ISSET(boardInfo[i].sd, &socketFDS)) {  // todo why not: if(boardInfo[gameId].sd != 0)
                uint8_t buffer[BUFFER_SIZE];
                int rc = (int) read(boardInfo[i].sd, &buffer, sizeof(buffer));

                if (rc == 0) { // the client disconnected normally
                    close(boardInfo[i].sd); // close the socket
                    initBoardInfo(&boardInfo[i]);
                } else if (rc < 0) {
                    perror("");
                    continue;
                } else {
                    if (rc < BUFFER_SIZE) {
                        printf("Received only %d bytes. (should have received %d bytes)\n", rc, BUFFER_SIZE);
                        perror("");
                        // todo
                    }
                    int processResult = processBuffer((uint8_t) i, buffer, boards);
                    if (processResult == 0) {  //todo

                    }
                }
            }
        }
    }
}

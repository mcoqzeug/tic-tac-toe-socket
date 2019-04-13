#include "tictactoe.h"


#define LOOP_CONTINUE 1
#define LOOP_BREAK 0


static uint8_t bufferRecv[BUFFER_SIZE];
static uint8_t bufferSend[BUFFER_SIZE];


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
 * return LOOP_CONTINUE or LOOP_BREAK
 */
int recvTimeLimit(int sd, int *resendCountPtr) {
    fd_set select_fds;
    struct timeval timeout;

    FD_ZERO(&select_fds);
    FD_SET(sd, &select_fds);

    timeout.tv_sec = TIME_LIMIT_CLIENT;
    timeout.tv_usec = 0;

    int selectResult = select(sd+1, &select_fds, NULL, NULL, &timeout) == 0;

    if (selectResult < 0) {
        perror("Failed to select: ");
        return LOOP_BREAK;
    }

    if (selectResult == 0) {  // time out, todo: resend?
        printf("No message in the past %d seconds. Resend.\n", TIME_LIMIT_CLIENT);
        sendBuffer(sd, bufferSend);
        *resendCountPtr += 1;
        return LOOP_CONTINUE;
    }

    memset(bufferRecv, 0, BUFFER_SIZE);
    int rc = read(sd, bufferRecv, BUFFER_SIZE);

    if (rc == 0) {
        printf("Server is disconnected.\n");
        return LOOP_BREAK;
    } else if (rc < BUFFER_SIZE) {
        printf("Received only %d bytes. (should have received %d bytes)\n", rc, BUFFER_SIZE);
        perror("Fail to read: ");
        return LOOP_BREAK;
    }
    return LOOP_CONTINUE;
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
                    (uint8_t) sendSequenceNum, board, SERVER_MARK);
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
        int *resendCountPtr,
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
        printf("Received invalid game id: %d.\n", gameId);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    // gameId is correct, check sequenceNum
    if (recvSequenceNum < expectedRecvSeqNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input
        if (*resendCountPtr < MAX_TRY) {
            printf("Received a duplicate packet, resend last msg.\n");
            *resendCountPtr += 1;
            sendBuffer(connected_sd, bufferSend);
            return LOOP_CONTINUE;
        }
        printf("Received a duplicate packet, run out of resend chances, exit game.\n");
        return LOOP_BREAK;
    }
    if (recvSequenceNum > expectedRecvSeqNum) {
        printf("Packets arrived out of order. Received sequence number: %d, expected: %d.\n",
               recvSequenceNum, expectedRecvSeqNum);
        respondToInvalidRequest(connected_sd, sendSequenceNum, gameId);
        return LOOP_BREAK;
    }
    // when gameId, port & ip, seqNum are all correct,
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
int buildGameForClient(int connected_sd) {
    int resendCount = 0;  // todo: this is useless
    for (int i=0; i<MAX_TRY+1; i++) {
        // send new game request
        uint8_t sb[BUFFER_SIZE] = {VERSION, 0, GAME_ON, 0, NEW_GAME, 0, 0};
        sendBuffer(connected_sd, sb);

        // receive response
        memset(bufferRecv, 0, sizeof bufferRecv);
        int recvResult = recvTimeLimit(connected_sd, &resendCount);
        if (recvResult != MALFORMED_REQUEST && recvResult != TIME_OUT) {
            // todo: need to check sequence number
            uint8_t recvSequenceNum = bufferRecv[6];

            // check sequence number
            if (recvSequenceNum < 1) {
                // receiving a duplicate packet means that the other
                // side might not have received my last msg, so do a resend
                printf("Received a duplicate packet, resend new game command.\n");
                continue;
            }
            if (recvSequenceNum > 1) {
                printf("Packets arrived out of order. Received sequence number: %d"
                       ", expected: %d.\n", recvSequenceNum, 1);
                continue;
            }
            uint8_t recvStatus = bufferRecv[2];
            uint8_t statusModifier = bufferRecv[3];
            if (recvStatus == GAME_ERROR) parseGeneralError(statusModifier);
            else return bufferRecv[5];
        }
        printf("Retry new game request.\n");
    }
    printf("Cannot build Game with server.\n");
    return -1;
}


void playClient(int connected_sd) {
    char board[ROWS][COLUMNS];
    initBoard(board);

    int buildGame = buildGameForClient(connected_sd);
    if (buildGame < 0) return;

    printBoard(board, CLIENT_MARK);
    uint8_t gameId = (uint8_t) buildGame;
    uint8_t sequenceNum = 2;
    int resendCount = 0;

    // client send the 1st move
    uint8_t choice = clientMakeChoice(board);
    int sendMoveResult = sendMoveWithChoice(
            connected_sd,
            choice,
            gameId,
            sequenceNum,
            board,
            CLIENT_MARK);
    if (sendMoveResult == GAME_ERROR) return;

    for (;;) {
        int recvResult = recvTimeLimit(connected_sd, &resendCount);
        if (recvResult == LOOP_BREAK) break;
        int processResult = processBufferClient(connected_sd, &resendCount, gameId, &sequenceNum, board);
        if (processResult == LOOP_BREAK) break;
    }
}

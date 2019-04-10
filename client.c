#include "tictactoe.h"


static uint8_t recvBuffer[BUFFER_SIZE];
static uint8_t sendBuffer[BUFFER_SIZE];


/*
* Function: recvTimeLimitForClient
* -------------------------------
*   Receive a message from a socket with timeout
*
*   connected_sd: socket descriptor
*
*   buffer: Points to the buffer where the message should be stored
*
*   return: returns TIME_OUT (4) if time out;
*   returns MALFORMED_REQUEST (2) if received invalid buffer
        *   returns 0 otherwise
*/
int recvTimeLimitForClient(
        int connected_sd,
        int time_out,
        uint8_t *buffer) {

    ssize_t count;
    for(;;) {
        fd_set select_fds;
        struct timeval timeout;

        FD_ZERO(&select_fds);
        FD_SET(connected_sd, &select_fds);

        timeout.tv_sec = time_out;
        timeout.tv_usec = 0;

        if (select(connected_sd + 1, &select_fds, NULL, NULL, &timeout) == 0) {
            printf("No message in the past %d seconds.\n", time_out);
            return TIME_OUT;
        } else {
            memset(buffer, 0, BUFFER_SIZE);
            count = read(connected_sd, buffer, BUFFER_SIZE);

            printf("RECEIVE choice: %d status: %d statusModifier: %d "
                   "gameType: %d gameId: %d sequenceNum: %d\n",
                   buffer[1], buffer[2], buffer[3],
                   buffer[4], buffer[5], buffer[6]);
            break;
        }
    }

    uint8_t version = buffer[0];

    if (count < BUFFER_SIZE) {
        printf("Received only %zu bytes. (should have received %d bytes)\n", count, BUFFER_SIZE);
        perror("");
        return MALFORMED_REQUEST;
    }
    if (version != VERSION) {
        printf("Received invalid version number: %d.\n", version);
        return MALFORMED_REQUEST;
    }
    return 0;
}


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
 *
 */
void respondToInvalidRequestClient(
        int connected_sd,
        int sendSequenceNum,
        uint8_t gameId) {
    uint8_t sb[BUFFER_SIZE] = {
            VERSION, 0, GAME_ERROR, MALFORMED_REQUEST, MOVE, gameId,
            (uint8_t) sendSequenceNum};
    sendChoice(connected_sd, sb);
}


/*
 * Function: receiveClient
 * ----------------------------
 *   Receive a move from the other node
 *
 *   connected_sd: socket file descriptor
 *
 *   gameId:
 *
 *   sequenceNumPtr: points to the address
 *   of the last sent sequence number
 *
 *   board[ROWS][COLUMNS]: the board for the game
 *
 *   mark: the mark for the opponent, either 'X' or 'O'
 *
 *
 *   return: game result, either GAME_ON (0), GAME_COMPLETE (1),
 *   GAME_ERROR (2), TRY_AGAIN (5), SKIP_NEXT_SEND (6)
 */
int receiveClient(
        int connected_sd,
        int *resendCount,
        uint8_t gameId,
        uint8_t *sequenceNumPtr,
        char board[ROWS][COLUMNS],
        char mark) {

    uint8_t buffer[BUFFER_SIZE];

    int recvResult = recvTimeLimitForClient(connected_sd, TIME_LIMIT_CLIENT, buffer);

    if (recvResult == TIME_OUT) {
        printf("Receive time out, run out of resend chances, exit game\n");
        return GAME_ERROR;
    }

    const int recvSequenceNum = buffer[6];
    const int expectedRecvSeqNum = (*sequenceNumPtr + 1) % 256;
    const int sendSequenceNum = (expectedRecvSeqNum + 1) % 256;

    if (recvResult == MALFORMED_REQUEST) {
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // #receivedBytes and #version are correct and no timeout
    // need to check seqNum, gameId

    // check sequenceNum
    if (recvSequenceNum < expectedRecvSeqNum) {
        // receiving a duplicate packet means that the other
        // side might not have received my last msg, so do a resend
        // and skip the next move input

        if (*resendCount < MAX_TRY) {
            printf("Received a duplicate packet, resend last msg. resendCount: %d\n", *resendCount);
            sendChoice(connected_sd, sendBuffer);
            *resendCount = *resendCount + 1;
            return SKIP_NEXT_SEND;
        }
        printf("Received a duplicate packet, run out of resend chances, exit game. resendCount: %d\n", *resendCount);
        return GAME_ERROR;
    }

    if (recvSequenceNum > expectedRecvSeqNum) {  // msg arrived out of order
        printf("Packets arrived out of order. "
               "Received sequence number: %d, expected: %d.\n",
               recvSequenceNum, expectedRecvSeqNum);
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // when recvSequenceNum is correct

    // update the value sequenceNumPtr is pointing to
    *sequenceNumPtr = (uint8_t) sendSequenceNum;

    // check if received game id equals local game id
    if (gameId != buffer[5]) {
        printf("Invalid board number.\n");
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // when gameId is correct
    uint8_t gameType = buffer[4];

    if (gameType < 1 || gameType > 2) {
        printf("Received invalid game type: %d.\n", gameType);
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    if (gameType == END_GAME) {
        int result = checkWin(board, mark);
        if (result == LOSE) printf("You win!\n");
        else printf("Draw.\n");
        return GAME_COMPLETE;
    }

    // when gameType == MOVE
    const uint8_t recvStatus = buffer[2];
    const uint8_t statusModifier = buffer[3];

    if (recvStatus == GAME_ERROR) {
        if (parseGeneralError(statusModifier) == 1)
            return TRY_AGAIN;
        return GAME_ERROR;
    }

    // when recvStatus == GAME_ON or GAME_COMPLETE

    // check if move is valid
    uint8_t choice = buffer[1];

    int row = (choice-1) / ROWS;
    int column = (choice-1) % COLUMNS;

    if (isMoveValid(board, row, column, choice) == 0) {
        printf("The opponent made an invalid move: %d.\n", choice);
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // update board
    board[row][column] = mark;

    char playerMark = (char)((mark == SERVER_MARK) ? CLIENT_MARK : SERVER_MARK);
    printBoard(board, playerMark);

    // check local game finished
    int result = checkWin(board, mark);

    if (recvStatus == GAME_ON) {
        if (result != GAME_ON)
            printf("Received invalid game status: %d, expected: %d.\n", recvStatus, GAME_ON);
        return GAME_ON;
    }

    if (recvStatus != GAME_COMPLETE) {
        printf("Received invalid game status: %d.\n", recvStatus);
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // check if local game and remote game has the same result
    if (result != statusModifier) {
        printf("Received invalid status modifier: %d. Expected: %d\n", statusModifier, result);
        respondToInvalidRequestClient(connected_sd, sendSequenceNum, gameId);
        return GAME_ERROR;
    }

    // send confirm
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
    int sendResult = sendChoice(
            connected_sd, sb);
    if (sendResult == 0) return GAME_ERROR;

    return GAME_COMPLETE;
}


/*
 * return -1 if there's an error, otherwise return gameId (should be a non-negative integer)
 */
int buildGameForClient(int connected_sd) {

    // todo: need to check sequence number

    for (int i=0; i<MAX_TRY+1; i++) {
        // send new game request
        uint8_t sb[BUFFER_SIZE] = {VERSION, 0, GAME_ON, 0, NEW_GAME, 0, 0};
        sendChoice(connected_sd, sb);

        memset(recvBuffer, 0, sizeof recvBuffer);
        int recvResult = recvTimeLimitForClient(connected_sd, TIME_LIMIT_CLIENT, recvBuffer);

        if (recvResult != MALFORMED_REQUEST && recvResult != TIME_OUT) {
            uint8_t recvStatus = recvBuffer[2];
            uint8_t statusModifier = recvBuffer[3];
            if (recvStatus == GAME_ERROR)
                parseGeneralError(statusModifier);
            else
                return recvBuffer[5];
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
    int receiveResult = GAME_ON;

    for (;;) {
        if (receiveResult != SKIP_NEXT_SEND) {
            uint8_t choice = clientMakeChoice(board);

            int sendMoveResult = sendMoveWithChoice(
                    connected_sd,
                    choice,
                    gameId,
                    sequenceNum,
                    board,
                    CLIENT_MARK);

            if (sendMoveResult == GAME_ERROR) break;
        }

        receiveResult = receiveClient(connected_sd, &resendCount, gameId, &sequenceNum,
                                      board, SERVER_MARK);

        if (receiveResult == GAME_ON)
            resendCount = 0;

            // if receive Result == Try again,
            // need to rebuild the game after waiting for time limit
        else if (receiveResult == TRY_AGAIN) {
            buildGame = buildGameForClient(connected_sd);

            if (buildGame < 0) return;
            gameId = (uint8_t) buildGame;
            resendCount = 0;
        }

            // receive Result == GAME_ERROR or GAME_COMPLETE or SKIP_NEXT_SEND
        else if (receiveResult != SKIP_NEXT_SEND)
            break;
    }
}

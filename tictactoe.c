#include "tictactoe.h"


/*
 * Function: checkWin
 * ----------------------------
 *   Check if someone wins, or if there is a draw, or if the game should go on
 *
 *   board[ROWS][COLUMNS]: the board
 *
 *   mark: the mark of the player who moved last
 *
 *   return: Returns GAME_ON (0), DRAW (1), WIN (2), or LOSE (3)
 */
int checkWin(char board[ROWS][COLUMNS], char mark) {
    if (board[0][0] == board[0][1] && board[0][1] == board[0][2]) {  // row matches
        return (mark == board[0][0]) ? WIN : LOSE;
    }
    else if (board[1][0] == board[1][1] && board[1][1] == board[1][2]) {  // row matches
        return (mark == board[1][0]) ? WIN : LOSE;
    }
    else if (board[2][0] == board[2][1] && board[2][1] == board[2][2]) {  // row matches
        return (mark == board[2][0]) ? WIN : LOSE;
    }
    else if (board[0][0] == board[1][0] && board[1][0] == board[2][0]) {  // column
        return (mark == board[0][0]) ? WIN : LOSE;
    }
    else if (board[0][1] == board[1][1] && board[1][1] == board[2][1]) {  // column
        return (mark == board[0][1]) ? WIN : LOSE;
    }
    else if (board[0][2] == board[1][2] && board[1][2] == board[2][2]) {  // column
        return (mark == board[0][2]) ? WIN : LOSE;
    }
    else if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) {  // diagonal
        return (mark == board[0][0]) ? WIN : LOSE;
    }
    else if (board[2][0] == board[1][1] && board[1][1] == board[0][2]) {  // diagonal
        return (mark == board[2][0]) ? WIN : LOSE;
    }
    else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
             board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' &&
             board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')
        return DRAW;
    else
        return GAME_ON;
}


/*
 * Function: printBoard
 * ----------------------------
 *   Print out the board and all the squares/values
 *
 *   board[ROWS][COLUMNS]: the board
 *
 *   mark: the current player's mark
 */
void printBoard(char board[ROWS][COLUMNS], char mark) {
    printf("\n\n\n\tCurrent TicTacToe Game\n\n");
    printf("Your mark is (%c)\n\n\n", mark);
    printf("     |     |     \n");
    printf("  %c  |  %c  |  %c \n", board[0][0], board[0][1], board[0][2]);
    printf("_____|_____|_____\n");
    printf("     |     |     \n");
    printf("  %c  |  %c  |  %c \n", board[1][0], board[1][1], board[1][2]);
    printf("_____|_____|_____\n");
    printf("     |     |     \n");
    printf("  %c  |  %c  |  %c \n", board[2][0], board[2][1], board[2][2]);
    printf("     |     |     \n\n");
}

/*
 * return 1 if statusModifier == TRY_AGAIN else return 0
 */
int parseGeneralError(uint8_t statusModifier) {
    if (statusModifier == OUT_OF_RESOURCES) {
        printf("Out of resources.\n");
        return 1;
    }
    else if (statusModifier == MALFORMED_REQUEST)
        printf("Malformed request.\n");
    else if (statusModifier == SERVER_SHUTDOWN)
        printf("Server shutdown.\n");
    else if (statusModifier == TIME_OUT)
        printf("Time out.\n");
    else if (statusModifier == TRY_AGAIN) {
        printf("Try again.\n");
        return 1;
    }
    else printf("Unknown error.\n");

    return 0;
}


void respondToInvalidRequest(
        int sd,
        int sendSequenceNum,
        uint8_t gameId) {

    uint8_t sb[BUFFER_SIZE] = {
            VERSION, 0, GAME_ERROR, MALFORMED_REQUEST, MOVE, gameId,
            (uint8_t) sendSequenceNum};

    sendBuffer(sd, sb);
}


/*
 * Function: sendBuffer
 * ----------------------------
 *   send choice for each step in the game
 *
 *   sd: socket descriptor
 *
 *   buffer:
 *
 *   target_address_pointer: the pointer pointing to a target socket address
 *
 *   return: returns 1 if send succeed, else 0
 */
int sendBuffer(int connected_sd, uint8_t buffer[BUFFER_SIZE]) {
    uint8_t nB[BUFFER_SIZE];
    nB[0] = buffer[0];
    nB[1] = buffer[1];
    nB[2] = buffer[2];
    nB[3] = buffer[3];
    nB[4] = buffer[4];
    nB[5] = buffer[5];
    nB[6] = buffer[6];  // todo: why?

    int writeResult = (int) write(connected_sd, &nB, sizeof(nB));

    if (writeResult < 0) {
        perror("Failed to send data");
        return 0;
    }

    printf("SEND choice: %d status: %d statusModifier: %d "
           "gameType: %d gameId: %d sequenceNum: %d\n",
           buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);

    return 1;
}

/*
 * Function: sendMoveWithChoice
 * ----------------------------
 *   Send a move to the other node, given a valid position
 *
 *   sd: socket file descriptor
 *
 *   uint8_t choice,
 *   uint8_t gameId,
 *   uint8_t sequenceNum,
 *   uint8_t buffer[BUFFER_SIZE],  // used to store the buffer last sent
 *   time_t *last_time,
 *
 *   board[ROWS][COLUMNS]: the board for the game
 *
 *   mark: the mark for the player, either 'X' or 'O'
 *
 *   opponent_address_pointer: the pointer pointing to a sockaddr structure containing the opponent's address
 *
 *   return: game status, either GAME_ON (0), or GAME_ERROR (2)
 */
int sendMoveWithChoice(
        int sd,
        uint8_t choice,
        uint8_t gameId,
        uint8_t sequenceNum,
        char board[ROWS][COLUMNS],
        char mark) {

    // 1. update board and check win
    int row = (choice-1) / ROWS;
    int column = (choice-1) % COLUMNS;

    board[row][column] = mark;
    printBoard(board, mark);

    int result = checkWin(board, mark);

    // 2. send msg
    int status = (result == GAME_ON) ? GAME_ON : GAME_COMPLETE;

    uint8_t sb[BUFFER_SIZE] = {
            VERSION, choice, (uint8_t) status, (uint8_t) result, MOVE,
            gameId, sequenceNum};

    if (sendBuffer(sd, sb) == 0) return GAME_ERROR;
    return GAME_ON;
}

/*
 * Function: isMoveValid
 * ----------------------------
 *   Check if a move overlaps with some previous move
 *
 *   board[ROWS][COLUMNS]: the board
 * 
 *   (row, column): the position of the new move
 * 
 *   choice: the position of the new move (1-9)
 *
 *   return: returns 0 if the new move is invalid, return 1 otherwise.
 */
int isMoveValid(char board[ROWS][COLUMNS], int row, int column, int choice) {
    if (choice > 9 || choice < 1) return 0;
    if (board[row][column] == (choice+'0')) return 1;
    else return 0;
}


/*
 * Function: initBoard
 * ----------------------------
 *   Initialize the board with values from 1 to 9
 *
 *   board[ROWS][COLUMNS]: the board
 */
void initBoard(char board[ROWS][COLUMNS]) {
    int i, j, count = 1;
    for (i=0; i<3; i++) {
        for (j = 0; j < 3; j++) {
            board[i][j] = (char) (count + '0');
            count++;
        }
    }
}


/*
 * Function: isDigitValid
 * ----------------------------
 *   check a given character is constructed by digits or not
 *   s: target string pointer
 *   return 1 if is valid digits else return 0
 */
int isDigitValid(const char *s) {
    while (*s) {
        if (*s >= '0' && *s <= '9') s++;
        else return 0;
    }
    return 1;
}


/*
 * Function: validate a given string is valid ip address or not
 * ----------------------------
 *   check a given character is constructed by digits or not
 *
 */
int isIpValid(const char *ip) {
    char ip_str[29];
    strcpy(ip_str, ip);

    int num, dots = 0;
    char *ptr = strtok(ip_str, DELIM);

    if (ip == NULL || ptr == NULL) return 0;

    while (ptr) {
        if (!isDigitValid(ptr)) return 0;

        num = (uint8_t) strtol(ptr, NULL, 10);

        if (num >= 0 && num <= 255) {
            ptr = strtok(NULL, DELIM);
            if (ptr != NULL) dots++;
        }
        else return 0;
    }
    return (dots == 3) ? 1 : 0;
}


/*
 * return 1 if port number is valid, else return 0
 */
int isPortNumValid(const char *portNum) {
    int len = (int) strlen(portNum);
    if (len <= 0) return 0;
    if (portNum[0] > '9' || portNum[0] < '1') return 0;
    for (int i = 1; i < strlen(portNum); i++)
        if (portNum[i] > '9' || portNum[i] < '0') return 0;
    return 1;
}

#include "tictactoe.h"


int main(int argc, char* argv[]) {
    int sd;
    long portNumber;
    struct sockaddr_in server_address;

    // check arguments
    if (argc != 2) {
        printf("usage: ./tictactoeServer <server_port>\n");
        exit(1);
    }

    if (isPortNumValid(argv[1]) == 1)
        portNumber = strtol(argv[1], NULL, 10);
    else {
        printf("Invalid port number\n");
        exit(1);
    }

    // start socket
    sd = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(portNumber);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(sd, (struct sockaddr *) & server_address, sizeof(server_address)) < 0) {
        perror("Connection failed: ");
        exit(-1);
    }

    if (listen(sd, 5) < 0) {
        perror("Fail to listen: ");
        close(sd);
        exit(-1);
    }

    playServer(sd);

    close(sd);
    return 0;
}

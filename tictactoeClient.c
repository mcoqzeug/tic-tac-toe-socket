#include "tictactoe.h"


int main(int argc, char* argv[]) {
    int sd;
    long serverPortNumber;
    long clientPortNumber;
    char serverIp[29];
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;

    // check arguments
    if (argc != 4 && argc != 3) {
        printf("usage: ./tictactoeClient <server_port> <server_ip> <client_port>\n");
        exit(1);
    }

    strcpy(serverIp, argv[2]);

    if (isIpValid(serverIp) == 0) {
        printf("Invalid ip address.\n");
        exit(1);
    }

    if (isPortNumValid(argv[1]) == 1)
        serverPortNumber = strtol(argv[1], NULL, 10);
    else {
        printf("Invalid port number\n");
        exit(1);
    }

    // start socket
    sd = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(serverPortNumber);
    server_address.sin_addr.s_addr = inet_addr(serverIp);

    if (argc == 4) {
        if (isPortNumValid(argv[3]) == 1)
            clientPortNumber = strtol(argv[3], NULL, 10);
        else {
            printf("Invalid port number\n");
            exit(1);
        }

        // bind
        client_address.sin_family = AF_INET;
        client_address.sin_port=htons(clientPortNumber); //source port for outgoing packets
        client_address.sin_addr.s_addr= htonl(INADDR_ANY);
        bind(sd, (struct sockaddr *) &client_address, sizeof(client_address));
    }

    if (connect(sd, (struct sockaddr *) &server_address, sizeof(struct sockaddr_in)) < 0) {
        close(sd);
        perror("Error");
        exit(1);
    }

    playClient(sd);

    close(sd);
    return 0;
}

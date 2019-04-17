#include "tictactoe.h"


int main(int argc, char* argv[]) {
    int sd_stream;
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

    // start stream socket
    sd_stream = socket(AF_INET, SOCK_STREAM, 0);
    if(sd_stream < 0) {
        perror("Opening stream socket error");
        exit(1);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(portNumber);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(sd_stream, (struct sockaddr *) & server_address, sizeof(server_address)) < 0) {
        perror("Connection failed: ");
        exit(-1);
    }

    if (listen(sd_stream, 5) < 0) {
        perror("Fail to listen: ");
        close(sd_stream);
        exit(-1);
    }

    // start datagram socket for multicast
    int sd_dgram = socket(AF_INET, SOCK_DGRAM, 0);
    if(sd_dgram < 0) {
        perror("Opening datagram socket error");
        exit(1);
    }

    struct sockaddr_in multicast_address;
    struct ip_mreq mreq;

    bzero((char *)&multicast_address, sizeof(multicast_address));

    multicast_address.sin_family = AF_INET;
    multicast_address.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_address.sin_port = htons(MC_PORT);

    if (bind(sd_dgram, (struct sockaddr *) &multicast_address, sizeof(multicast_address)) < 0) {
        perror("bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr =	inet_addr(MC_GROUP);
    mreq.imr_interface.s_addr =	htonl(INADDR_ANY);

    if (setsockopt(sd_dgram, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt mreq");
        exit(1);
    }

    playServer(sd_stream, sd_dgram, portNumber);

    close(sd_stream);
    close(sd_dgram);
    return 0;
}

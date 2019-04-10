# Lab 7

Team members: Juanxi Li, Wei Da

To compile:

```bash
make
```

To run server:

```bash
./tictactoeServer <server_port>
```

e.g.

```bash
./tictactoeServer 24000
```

To run client:

```bash
./tictactoeClient <server_ip> <server_port> 
```

e.g. (locally)

```bash
./tictactoeClient 127.0.0.1 24000
```


## Design

Byte priority:
1st byte, version;
5th byte, game type / command;
7th byte, sequence number;
3rd byte, game status;
...



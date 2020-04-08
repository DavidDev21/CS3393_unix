# CS3393 - Unix Systems Programming
## Homework 5 - Two Way Chat By David Zheng

There should be no space in the username

### server.c
Usage: ./server <name for the user> [-p for the port]

Example:
    ./server I_Am_Server -p 8080

### client.c
Usage: ./client <name for the user> [-s for the server IPv4 address] [-p for the port of the server]

The order of the flags don't matter, both are optional
Example:
    ./client I_am_Client -s 127.0.0.1 -p 8080


Defaults:
Server listens on port 8920, on any INADDR_ANY (so any address that can get to the machine)

Client attempts to connect to 127.0.0.1 on port 8920

# EXIT
You can exit with just ctrl + c

# CS3393 - Unix Systems Programming
## Homework 6 - Chatroom By David Zheng

There should be no space in the username

### server.c
Usage: ./server [-p for the port]

Example:
    ./server -p 8080

### client.c
Usage: ./client <name for the user> [-s for the server IPv4 address] [-p for the port of the server]

The order of the flags don't matter, both are optional
Example:
    ./client I_am_Client -s 127.0.0.1 -p 8080
    ,/client I_am_Client -p 8080


Defaults:
Server listens on port 8920, on any INADDR_ANY (so any address that can get to the machine)

Client attempts to connect to 127.0.0.1 on port 8920

# EXIT
1.) You can exit with just ctrl + c
2.) Type in "QUIT" on client side

# Fundamental changes
1.) I made both the client and server able to handle any length of messages, whether
they are sending or receiving. Previously it was restricted to the size of the buffer
Now, changing the buffer size.
2.) Removed signal handling
3.) Changed server to manage messages from multiple clients rather than act like a client

# Client changes
I added logic for quitting when user types in "QUIT"

# Server changes
1.) Removed server's ability to act like a client
2.) Added a lot of functions to manage server tasks
3.) Added dynamic_string struct for strings of unknown length
4.) Multi-threading, 1 Broadcast thread (consumer), X Client threads (producer)

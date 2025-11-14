
            --==== CREDITS ====--
This code was made by Logan Kinzler for YCP's CS330
course, taught during the 2025 Fall semester by Galin*
Zhelezov.


          --==== DESCRIPTION ====--
These client & server files are used in tandem to form
a chatroom of uniquely-named users that can converse
with each other.

Every user is notified when other users enter or leave
the chat room and are able to see a list of users by
name. Every message sent is paired with the name of the
user for identification of message source.


        --==== COMPILE INSTRUCTIONS ====--
To compile each of these files, meant for Ubuntu in
Linux, run the '' command in an Ubuntu terminal within
a directory containing the C files:

For the client:
 gcc -o [output file name] client.c -lpthread

For the server:
 gcc -o [output file name] server.c -lpthread

Of course, to initialize the chatroom, run the server
file first, and then clients can join through running
the client output files.


      --==== CHATROOM CLIENT COMMANDS ====--

The commands are begun with '~!' and then followed by
the command in all caps and underscores.

List of Commands:
1.  ~!QUIT
 This command is used for the current user to leave the
 chatroom. All other users are notified by the server.

2.  ~!LIST_NAMES
 This command lists out the names of the users currently
 in the chatroom excluding the current user. The names
 are listed 4 per line.


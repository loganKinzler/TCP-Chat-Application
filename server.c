#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>


struct client_info
{
    int fd;
    int is_prepped;
    char name[16];
};

int clients_are_equal(const struct client_info* c1, const struct client_info* c2)
{
    return c1->fd == c2->fd && c1->is_prepped == c2->is_prepped && strcmp(c1->name, c2->name) == 0;
}

struct client_info* remove_client(struct client_info* d_arr, int* arr_size, int i)
{
    if (d_arr == NULL || arr_size == NULL)
        return NULL;

    if (*arr_size <= 0 || i < 0 || i >= *arr_size)
        return NULL;

    if (*arr_size == 1) {
        free(d_arr);
        *arr_size = 0;
        return NULL;
    }

    for (int r=i; r<*arr_size-1; r++)
        d_arr[r] = d_arr[r + 1];

    struct client_info* failsafe = d_arr;
    struct client_info* temp = realloc(d_arr, (*arr_size - 1)*sizeof(struct client_info));

    if (temp == NULL)
    {
        perror("Reallocation failure.");
        return failsafe;
    }

    *arr_size = *arr_size - 1;
    return temp;
}

struct client_info* add_client(struct client_info* d_arr, int* arr_size)
{
    if (arr_size == NULL)
        return NULL;

    struct client_info* temp = realloc(d_arr, (*arr_size + 1)*sizeof(struct client_info));

    if (temp == NULL)
    {
        perror("Rellocation failure");
        return d_arr;
    }

    memset(&temp[*arr_size], 0, sizeof(struct client_info));
    *arr_size = *arr_size + 1;

    return temp;
}

int find_client_by_name(const struct client_info* clients, const int num_clients, const char* find_name)
{
    if (clients == NULL || num_clients <= 0)
        return -1;

    for (int c=0; c<num_clients; c++)
    {
        if (strcmp(clients[c].name, find_name) == 0)
            return c;
    }

    return -1;
}

int find_client_by_fd(const struct client_info* clients, const int num_clients, const int find_fd)
{
    if (clients == NULL || num_clients <= 0)
        return -1;

    for (int c=0; c<num_clients; c++)
    {
        if (clients[c].fd == find_fd)
            return c;
    }

    return -1;
}

void send_message_to_all_clients(const struct client_info* clients, const int num_clients, const int sent_index, const char* msg_buffer)
{
    if (clients == NULL)
        return;

    for (int n=0; n<num_clients; n++)
    {
        if (n == sent_index)
            continue;

        send(clients[n].fd, msg_buffer, strlen(msg_buffer) + 1, 0);
    }
}


int message_is_command(const char* client_message)
{
    if (client_message == NULL)
        return 0;

    return strncmp(client_message, "~!", 2) == 0;
}

int name_is_unique(const char* unique_name, const struct client_info* clients, int array_size)
{
    if (clients == NULL || array_size <= 0)
        return 1;

    for (int n=0; n<array_size; n++)
        if (strcmp(clients[n].name, unique_name) == 0)
            return 0;

    return 1;
}


void main()
{
    // constants
    int sock_addr_byte_size = sizeof(struct sockaddr_in);

    // reserve a socket
    int chat_socket;
    if ((chat_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket error\n");
        return;
    }


    // specify socket to be reused
    int reuse = 1;
    if (setsockopt(chat_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt error\n");
        return;
    }


    // collect server (self) & client address data
    struct sockaddr_in server;
    struct sockaddr_in client;
    int sockaddr_size = sizeof(struct sockaddr_in);
    memset(&server, 0x00, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(9090);


    // bind to selected socket
    if (bind(chat_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        printf("Bind error\n");
        return;
    }


    // setup multiclient
    fd_set super_fd, sub_fd;
    int client_quantity = 1;
    int nfds = 0;
    int max_name_size = 16;
    struct client_info* clients = malloc(sizeof(struct client_info));

    if (clients == NULL)
    {
        perror("Memory allocation failed.");
        close(chat_socket);
        return;
    }

    struct client_info server_info = {0, 1, "Server"};
    clients[0] = server_info;
    
    FD_ZERO(&super_fd);
    FD_SET(chat_socket, &super_fd);
    nfds = chat_socket;


    // listen on selected socket
    listen(chat_socket, 5);

    printf("\n--==== Chatroom Opened ====--\n\n");


    // setup message buffer
    char client_message[64 + 16 + 4];
    char shared_message[64 + 16 + 4];

    memset(client_message, 0x00, sizeof(client_message));
    memset(shared_message, 0x00, sizeof(shared_message));


    // continuously send and receive chat messages
    while (1)
    {
        // add new sub process
        sub_fd = super_fd;

        if (select(nfds + 1, &sub_fd, NULL, NULL, NULL) < 0)
        {
            perror("Select Error\n");
            break;
        }


        // run through sub processes
        for (int c_fd=0; c_fd<=nfds; c_fd++)
        {
            // avoid unset file descriptors
            if (!FD_ISSET(c_fd, &sub_fd))
                continue;


            // check for new client and add them to array
            if (c_fd == chat_socket)
            {
                // connect to new client
                int client_sock = accept(chat_socket, (struct sockaddr*)&client, (socklen_t*) &sock_addr_byte_size);

                int prior_nfds = nfds;
                FD_SET(client_sock, &super_fd);
                if (nfds < client_sock)
                    nfds = client_sock;

                int prior_client_quantity = client_quantity;
                clients = add_client(clients, &client_quantity);
                if (client_quantity == prior_client_quantity)
                {
                    perror("Memory allocation error");

                    // don't shut everything down, but shut down connection to single (new) client
                    FD_CLR(client_sock, &super_fd);
                    nfds = prior_nfds;
                    continue;
                }

                clients[client_quantity - 1].is_prepped = 0;
                clients[client_quantity - 1].fd = client_sock;

                continue;
            }

            int client_index = find_client_by_fd(clients, client_quantity, c_fd);
            struct client_info* client_info = &clients[client_index];

            // current clients
            // receive incoming message from client
            long bytes = recv(c_fd, client_message, sizeof(client_message), 0);


            // if user left
            if (bytes <= 0)
            {
                // close socket
                close(c_fd);
                FD_CLR(c_fd, &super_fd);

                // actual error, not just the client severing connection
                if (bytes < 0)
                    perror("Receive failure\n");


                // let other users know that someone has left
                if (client_info->is_prepped == 1)
                {
                    char leave_message[64] = "";
                    sprintf(leave_message, "[Server]: '%s' has left the chat.", client_info->name);

                    printf("%s\n", leave_message);
                    send_message_to_all_clients(clients, client_quantity, client_index, leave_message);
                }


                // reset arrays for c_fd
                int prior_client_quantity = client_quantity;
                clients = remove_client(clients, &client_quantity, client_index);

                if (prior_client_quantity == client_quantity)
                {
                    perror("Remove client error");
                    break;
                }


                continue;
            }


            // client did not enter a command (assumed prepped)
            if (!message_is_command(client_message))
            {
                // add to server logs
                printf("%s\n", client_message);

                // add to client logs
                send_message_to_all_clients(clients, client_quantity, client_index, client_message);
                continue;
            }

            // client entered command


            // user is changing name (for now only client system)
            if (strncmp(client_message, "~!SET_NAME '", 12) == 0 && client_message[strlen(client_message) - 1] == '\'' && client_info->is_prepped == 0)
            {
                char entered_name[16] = "";
                int response = 1;

                strcpy(entered_name, client_message + (long)12);
                entered_name[strlen(entered_name) - 1] = '\0';

                // user provides unique name
                if (name_is_unique(entered_name, clients, client_quantity) == 1)
                {
                    // set client name
                    strcpy(client_info->name, entered_name);

                    // tell client that name is unique
                    if (send(c_fd, &response, sizeof(response), 0) < 0)
                        perror("Send failure.\n");

                    continue;
                }

                response = 0;

                // tell client that name is not unique
                if (send(c_fd, &response, sizeof(response), 0) < 0)
                    perror("Send failure.\n");
                continue;
            }


            // user finishes profile setup (only allowed by client system, not client user)
            if (strcmp(client_message, "~!NAME_CONFIRMED") == 0 && client_info->is_prepped == 0)
            {
                // prep client
                client_info->is_prepped = 1;

                // let users know that someone has entered
                char enter_message[64] = "";
                sprintf(enter_message, "[Server]: '%s' has entered the chat.", client_info->name);

                printf("%s\n", enter_message);
                send_message_to_all_clients(clients, client_quantity, client_index, enter_message);
                continue;
            }


            char command_response[64 + 16 + 4] = "";

            if (strcmp(client_message, "~!LIST_NAMES") == 0)
            {
                // send first line
                strcpy(command_response, "--==== USERS ONLINE ====--");
                if (send(c_fd, command_response, strlen(command_response) + 1, 0) < 0)
                {
                    perror("Send error;");
                    break;
                }

                usleep(10000);
                memset(command_response, 0x00, sizeof(command_response));


                int send_failure = 0;
                int printed_names = 0;

                // get all names
                for (int u=0; u<client_quantity; u++)
                {
                    // don't print out server or client who entered command
                    if (clients_are_equal(&clients[u], client_info) == 1 || clients_are_equal(&clients[u], &server_info) == 1)
                        continue;

                    // add new names with commas at the end
                    char additional_name[20] = "";
                    printed_names++;

                    // set up lines vs continue lines
                    if (printed_names % 4 == 1)
                        sprintf(additional_name, "    %s", clients[u].name);
                    else
                        sprintf(additional_name, ", %s", clients[u].name);

                    strcat(command_response, additional_name);

                    // every line send 4 users at a time
                    if ((printed_names - 1)%4 != 0 || printed_names == 1)
                        continue;

                    // send and then prep next name
                    if (send(c_fd, command_response, strlen(command_response) + 1, 0) < 0)
                    {
                        perror("Send failure.");
                        send_failure = 1;
                        break;
                    }

                    usleep(10000);
                    memset(command_response, 0x00, sizeof(command_response));
                }

                if (send_failure == 1)
                    break;

                // don't send repeated names (full set of names just sent or no names to send)
                if (strlen(command_response) == 0)
                    continue;


                // send last batch if they exist
                if (send(c_fd, command_response, strlen(command_response) + 1, 0) < 0)
                {
                    perror("Send failure.");
                    send_failure = 1;
                    break;
                }

                usleep(10000);
                continue;
            }
        }
    }

    printf("\n\n--==== Chatroom Closed ====--");

    // close server's (self's) socket
    if (clients != NULL)
        free(clients);

    close(chat_socket);
}
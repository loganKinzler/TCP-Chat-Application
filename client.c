#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <termios.h>


pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void print_msg_in_logs(char* log, char* input_buffer)
{
    printf("\033[1A\r%s\n\033[2K\nEnter a Message: %s", log, input_buffer);
}


int message_is_command(char* in_buffer)
{
    return strstr(in_buffer, "~!") - (long)in_buffer == 0;
}

typedef struct
{
    int server_fd;
    char* client_msg_buffer;
    pthread_mutex_t lock;
} recv_msg_args;

void* receive_messages_from_server(void* args)
{
    recv_msg_args* recv_args =  (recv_msg_args*) args;
    int server_fd = recv_args->server_fd;
    char** buffered_client_msg = &recv_args->client_msg_buffer;
    pthread_mutex_t* input_lock = &recv_args->lock;

    char incoming_message[64 + 16 + 4];


    // continuously receive messages from the server
    while (recv(server_fd, incoming_message, sizeof(incoming_message), 0) > 0)
    {
        pthread_mutex_lock(&print_lock);
        pthread_mutex_lock(input_lock);
        print_msg_in_logs(incoming_message, *buffered_client_msg);
        fflush(stdout);
        pthread_mutex_unlock(input_lock);
        pthread_mutex_unlock(&print_lock);
    }

    pthread_exit(NULL);
}


void clearInputBuffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void get_input_sequentially(char* input_buffer, const int buffer_size, pthread_mutex_t* input_lock)
{
    char c;
    int i = 0;

    while (i < buffer_size - 1)
    {
        read(STDIN_FILENO, &c, 1);

        // backspace
        if ((c == 127 || c == 8) && i > 0)
        {
            pthread_mutex_lock(input_lock);
            input_buffer[i - 1] = '\0';
            pthread_mutex_unlock(input_lock);

            pthread_mutex_lock(&print_lock);
            printf("\b \b");
            fflush(stdout);
            pthread_mutex_unlock(&print_lock);

            i--;
        }

        // non special key
        else if (c >= 32 && c <= 126)
        {
            pthread_mutex_lock(input_lock);
            input_buffer[i] = c;
            pthread_mutex_unlock(input_lock);

            pthread_mutex_lock(&print_lock);
            printf("%c", c);
            fflush(stdout);
            pthread_mutex_unlock(&print_lock);

            i++;
        }

        // enter key (end input)
        else if (c == '\n')
            break;
    }
}


int is_valid_name(const int dest, const char* name_buffer, const int name_size)
{
    char set_name_command[32];
    sprintf(set_name_command, "~!SET_NAME '%s'", name_buffer);

    if (send(dest, set_name_command, 32, 0) < 0)
    {
        perror("Send failure.\n");
        close(dest);
        return 0;
    }

    int is_valid = 0;
    if (recv(dest, &is_valid, sizeof(int), 0) < 0)
    {
        perror("Receive failure.\n");
        close(dest);
        return 0;
    }

    return is_valid;
}

void get_unique_name(const int dest, char* name_buffer, const int name_buffer_size)
{
    char accept;

    do
    {
        printf("\nProvide a Name: ");
        fgets(name_buffer, name_buffer_size, stdin);
        name_buffer[strcspn(name_buffer, "\r\n")] = '\0';

        while (is_valid_name(dest, name_buffer, name_buffer_size) == 0)
        {
            printf("'%s' is already in use, try again: ", name_buffer);
            fgets(name_buffer, name_buffer_size, stdin);
            name_buffer[strcspn(name_buffer, "\r\n")] = '\0';
        }

        printf("Name provided: '%s'\n", name_buffer);

        do
        {
            printf("Accept? (y/n): ");
            scanf(" %c", &accept);
            clearInputBuffer();
        }
        while (accept != 'y' && accept != 'n');
    }
    while (accept != 'y');

    // wait to confirm name here
    if (send(dest, "~!NAME_CONFIRMED", 17, 0) < 0)
        perror("Send failure");
}


void main()
{
    // reserve a socket
    int server_sock;
    if ((server_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket error\n");
        return;
    }


    // collect server address data
    struct sockaddr_in server_addr_in;
    int sockaddr_size = sizeof(struct sockaddr_in);
    memset(&server_addr_in, 0x00, sizeof(server_addr_in));

    server_addr_in.sin_family = AF_INET;
    server_addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr_in.sin_port = htons(9090);


    // connect to server across reserved socket
    if (connect(server_sock, (struct sockaddr *)&server_addr_in, sockaddr_size) < 0)
    {
        perror("Connection failure\n");
        close(server_sock);
        return;
    }

    // setup user profile
    char client_name[16] = "";

    printf("\n--==== Profile Setup ====--\n");
    get_unique_name(server_sock, client_name, sizeof(client_name));
    printf("\n--=======================--\n\n\n\n");


    // setup messages (incoming / outgoing)
    char client_input[64];
    char client_message[64];
    char logs_message[64 + 16 + 4];
    char outgoing_message[64 + 16 + 4];

    memset(logs_message, 0x00, sizeof(logs_message));
    memset(client_input, 0x00, sizeof(client_input));
    memset(client_message, 0x00, sizeof(client_message));
    memset(outgoing_message, 0x00, sizeof(outgoing_message));


    // prevent user input from showing
    enable_raw_mode();


    // setup multiclient
    pthread_t recv_thread;

    recv_msg_args shared_args;
    shared_args.server_fd = server_sock;
    shared_args.client_msg_buffer = client_input;

    pthread_mutex_init(&shared_args.lock, NULL);
    pthread_create(&recv_thread, NULL,
        receive_messages_from_server, (void*) &shared_args);


    // get first outgoing message from the console
    pthread_mutex_lock(&print_lock);
    printf("Enter a Message: ");
    fflush(stdout);
    pthread_mutex_unlock(&print_lock);

    get_input_sequentially(client_input, sizeof(client_input), &shared_args.lock);

    pthread_mutex_lock(&shared_args.lock);
    strcpy(client_message, client_input);
    memset(client_input, 0x00, sizeof(client_input));
    pthread_mutex_unlock(&shared_args.lock);


    sprintf(logs_message, "[%s]: %s", client_name, client_message);

    if (message_is_command(client_message))
        strcpy(outgoing_message, client_message);
    else
        strcpy(outgoing_message, logs_message);


    pthread_mutex_lock(&print_lock);
    pthread_mutex_lock(&shared_args.lock);

    print_msg_in_logs(logs_message, client_input);
    fflush(stdout);

    pthread_mutex_unlock(&shared_args.lock);
    pthread_mutex_unlock(&print_lock);


    // continuously use socket after every sent message
    while (strcmp(client_message, "~!QUIT") != 0)
    {

        // send outgoing message
        if (strcmp(client_message, "") != 0)
        {
            if (send(server_sock, outgoing_message, strlen(outgoing_message) + 1, 0) < 0)
            {
                perror("Send failure\n");
                close(server_sock);
                return;
            }
        }


        // continuously get user input
        get_input_sequentially(client_input, sizeof(client_input), &shared_args.lock);

        pthread_mutex_lock(&shared_args.lock);
        strcpy(client_message, client_input);
        memset(client_input, 0x00, sizeof(client_input));
        pthread_mutex_unlock(&shared_args.lock);


        sprintf(logs_message, "[%s]: %s", client_name, client_message);

        if (message_is_command(client_message))
            strcpy(outgoing_message, client_message);
        else
            strcpy(outgoing_message, logs_message);


        // go back to same line before continuing (expecting to be on the same line as "Enter a message"
        pthread_mutex_lock(&print_lock);
        print_msg_in_logs(logs_message, client_input);
        fflush(stdout);
        pthread_mutex_unlock(&print_lock);
    }

    // end program
    close(server_sock);
}
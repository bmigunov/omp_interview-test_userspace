#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>
#include <string.h>
#include "chat.h"




struct chat_client connected_clients[MAX_CLIENTS];
unsigned char      count = 0;
pthread_mutex_t    mut = PTHREAD_MUTEX_INITIALIZER;


int main(int argc, char **argv)
{
    char opt;

    opt = getopt(argc, argv, "chs");
    switch(opt)
    {
        case 'c':
            printf("Client mode\n");
            if(start_client())
            {
                perror("Client issue\n");
                exit(0);
            }
            break;

        case 'h':
            printf("Usage:\n-c: launch as client;\n-s: launch as server;\n-h:" \
                   " this message\n");
            exit(0);

        case 's':
            printf("Server mode\n");
            if(start_server())
            {
                perror("Server issue\n");
                exit(0);
            }
            break;

        default:
            printf("Use -h option to see how to use this app.\n");
            exit(0);
    }

    return 0;
};


int start_server(void)
{
    struct sockaddr_in server_addr;
    ssize_t            bytes_read, bytes_sent;
    int                lockfile, server_socket, connection_socket;
    pthread_t          *thread_id, *thread_ptr;
    unsigned char      n;

    lockfile = open(CHAT_LOCKFILE_PATH, O_CREAT | O_RDWR, S_IRUSR);
    if(lockfile < 0)
    {
        printf("One instance of server is already running\n");
        return -1;
    }
    flock(lockfile, LOCK_EX);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0)
    {
        close(lockfile);
        unlink(CHAT_LOCKFILE_PATH);
        perror("Server socket creation issue\n");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6776);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(server_socket, (struct sockaddr *) &server_addr,
            sizeof(server_addr)) < 0)
    {
        close(lockfile);
        unlink(CHAT_LOCKFILE_PATH);
        perror("Error binding socket to local interface\n");
        return -1;
    }

    if(listen(server_socket, MAX_PENDING_CONNS))
    {
        close(lockfile);
        unlink(CHAT_LOCKFILE_PATH);
        perror("Could set server's listening socket to passive state\n");
        return -1;
    }

    thread_id = malloc(sizeof(pthread_t));

    while(1)
    {
        connected_clients[count].addrlen = sizeof(struct sockaddr_in);
        connected_clients[count].sock = accept(server_socket,
                                               (struct sockaddr *) &connected_clients[count].addr,
                                               &connected_clients[count].addrlen);
        if(connected_clients[count].sock < 0)
        {
            perror("Could not create the socket associated with peer\n");
            continue;
        }

        if(register_user_server(count))
        {
            printf("Could not register user\n");
            if(close(connection_socket))
            {
                close(server_socket);
                return -1;
            }
            continue;
        };

        ++count;
        n = count - 1;
        if(count > 1)
        {
            thread_ptr = realloc(thread_id, sizeof(pthread_t) * count);
            thread_id = thread_ptr;
        }
        pthread_create(thread_id + count - 1, NULL, handle_user, &n);
        pthread_detach(*(thread_id + count - 1));
    }

    close(lockfile);
    unlink(CHAT_LOCKFILE_PATH);
    close(connection_socket);
    close(server_socket);
    return 0;
};

void *handle_user(void *n)
{
    struct pollfd pfd;
    ssize_t       bytes_read, bytes_sent;
    char          message_buffer[MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2)];
    char          message_to_clients[MAX_MESSAGE_LENGTH];
    unsigned char i, num;

    num = *((unsigned char *) n);

    pfd.fd = connected_clients[num].sock;
    pfd.events = POLLIN;
    pthread_mutex_lock(&mut);
    if(fcntl(connected_clients[num].sock, F_SETFL, O_NONBLOCK))
    {
        pthread_mutex_unlock(&mut);
        close(connected_clients[num].sock);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&mut);

    while(1)
    {
        if(poll(&pfd, 1, 200) > 0)
        {
            if(pfd.revents & POLLIN)
            {
                pthread_mutex_lock(&mut);
                bytes_read = recv(connected_clients[num].sock, message_buffer,
                                  MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2),
                                  0);
                if(bytes_read <= 0)
                {
                    close(connected_clients[num].sock);
                    --count;
                    pthread_mutex_unlock(&mut);
                    pthread_exit(NULL);
                }
                pthread_mutex_unlock(&mut);

                memset(message_to_clients, '\0', MAX_MESSAGE_LENGTH);
                strcat(message_to_clients, connected_clients[num].username);
                strcat(message_to_clients, ": ");
                strcat(message_to_clients, message_buffer);

                pthread_mutex_lock(&mut);
                for(i = 0; i < count; ++i)
                {
                    if(i == num)
                    {
                        continue;
                    }
                    bytes_sent = send(connected_clients[i].sock,
                                      message_to_clients, MAX_MESSAGE_LENGTH,
                                      0);
                }
                pthread_mutex_unlock(&mut);
                memset(message_buffer, '\0', MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2));
            }
        }
    }

    pthread_exit(NULL);
};

int register_user_server(unsigned char i)
{
    ssize_t       bytes_read, bytes_sent;
    unsigned char j;
    char          username[USERNAME_STRLEN];
    char          registration_state;

    while(1)
    {
        registration_state = REGISTRATION_SUCCESS;

        bytes_read = recv(connected_clients[i].sock, username, USERNAME_STRLEN,
                          0);
        if(bytes_read <= 0)
        {
            return REGISTRATION_FAILURE;
        }

        for(j = 0; j < MAX_CLIENTS; ++j)
        {
            if(!strncmp(connected_clients[j].username, username,
                        USERNAME_STRLEN))
            {
                registration_state = REGISTRATION_FAILURE;
                bytes_sent = send(connected_clients[j].sock,
                                  &registration_state, sizeof(char), 0);
                if(bytes_sent <= 0)
                {
                    return REGISTRATION_FAILURE;
                }
                break;
            }
        }

        if(registration_state == REGISTRATION_SUCCESS)
        {
            strncpy(connected_clients[i].username, username, USERNAME_STRLEN);
            bytes_sent = send(connected_clients[i].sock, &registration_state,
                              sizeof(char), 0);
            if(bytes_sent <= 0)
            {
                return REGISTRATION_FAILURE;
            }

            printf("User %s registered\n", username);
            return REGISTRATION_SUCCESS;
        }
    }

    return REGISTRATION_FAILURE;
};

int start_client(void)
{
    struct sockaddr_in server_addr;
    struct pollfd      pfd;
    pthread_t          thread_id;
    ssize_t            bytes_sent;
    int                message_len;
    int                connection_socket;
    char               username[USERNAME_STRLEN];
    char               server_addr_str[IP_ADDR_STRLEN];
    char               message_buffer[MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2)];

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6776);

    printf("Server IP\n");
    scanf("%s", server_addr_str);
    getchar();
    while(inet_pton(AF_INET, server_addr_str, &server_addr.sin_addr) != 1)
    {
        memset(server_addr_str, '\0', IP_ADDR_STRLEN);
        printf("Invalid server IP, retype\n");
        scanf("%s", server_addr_str);
    }

    connection_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(connection_socket < 0)
    {
        perror("Issue creating socket\n");
        return -1;
    }

    if(connect(connection_socket, (struct sockaddr *) &server_addr,
               sizeof(server_addr)))
    {
        perror("Could not connect to server\n");
        return -1;
    }

    if(register_user_client(username, &connection_socket))
    {
        printf("Client registration error\n");
        return -1;
    }

    if(fcntl(connection_socket, F_SETFL, O_NONBLOCK))
    {
    }
    pfd.fd = connection_socket;
    pfd.events = POLLOUT;

    message_len = MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2);

    pthread_create(&thread_id, NULL, handle_server, &connection_socket);
    pthread_detach(thread_id);

    while(1)
    {
        fgets(message_buffer, message_len, stdin);

        if(!strncmp(message_buffer, DISCONNECT_CMD,
                    MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2)))
        {
            close(connection_socket);
            break;
        }
        if(!strncmp(message_buffer, QUIT_CMD,
                    MAX_MESSAGE_LENGTH - (USERNAME_STRLEN + 2)))
        {
            close(connection_socket);
            exit(0);
        }

        if(poll(&pfd, 1, 200) > 0)
        {
            if(pfd.revents & POLLOUT)
            {
                bytes_sent = send(connection_socket, message_buffer, message_len, 0);
                if(bytes_sent <= 0)
                {
                    perror("Sending error\n");
                    close(connection_socket);
                    return -1;
                }
                memset(message_buffer, '\0', MAX_MESSAGE_LENGTH);
            }
        }
    }

    return 0;
};

void *handle_server(void *sock)
{
    struct  pollfd pfd;
    ssize_t bytes_read;
    int     connection_socket;
    char    message_buffer[MAX_MESSAGE_LENGTH];

    connection_socket = *((int *) sock);
    pfd.fd = connection_socket;
    pfd.events = POLLIN;

    while(1)
    {
        if(poll(&pfd, 1, 200) > 0)
        {
            if(pfd.revents & POLLIN)
            {
                bytes_read = recv(connection_socket, message_buffer,
                                  MAX_MESSAGE_LENGTH, 0);
                if(bytes_read <= 0)
                {
                    perror("Recv error\n");
                    close(connection_socket);
                    pthread_exit(NULL);
                }
                printf("%s\n", message_buffer);
                memset(message_buffer, '\0', MAX_MESSAGE_LENGTH);
            }
        }
    }
};

int register_user_client(char *username, int *sock)
{
    ssize_t bytes_read, bytes_sent;
    char    registration_state;

    while(1)
    {
        printf("Username\n");
        scanf("%s", username);
        getchar();

        bytes_sent = send(*sock, username, USERNAME_STRLEN, 0);
        if(bytes_sent <= 0)
        {
            perror("Username regitration issue\n");
            return REGISTRATION_FAILURE;
        }

        bytes_read = recv(*sock, &registration_state, sizeof(char), 0);
        if(registration_state != REGISTRATION_SUCCESS)
        {
            printf("Such username already registered on the server\n");
        }
        else
        {
            printf("Registration succeeded\n");
            return REGISTRATION_SUCCESS;
        }
    }

    return REGISTRATION_FAILURE;
};

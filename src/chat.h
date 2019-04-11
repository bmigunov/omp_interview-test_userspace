#ifndef __CHAT_H
#define __CHAT_H


#define USERNAME_STRLEN      32
#define MAX_MESSAGE_LENGTH   256
#define MAX_CLIENTS          32
#define MAX_PENDING_CONNS    8
#define IP_ADDR_STRLEN       16
#define REGISTRATION_SUCCESS 0
#define REGISTRATION_FAILURE -1

#define DISCONNECT_CMD "/disconnect\n"
#define QUIT_CMD       "/quit\n"
#define CHAT_LOCKFILE_PATH ".chat.lock"




struct chat_client
{
    struct sockaddr_in addr;
    socklen_t          addrlen;
    int                sock;
    char               username[USERNAME_STRLEN];
};


int  is_server_running(void);
int  start_server(void);
int  register_user_server(unsigned char);
int  register_user_client(char *, int *);
int  start_client(void);
void *handle_user(void *);
void *handle_server(void *);


#endif /* __CHAT_H */

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 1024

int
main()
{
    int fd;
    char buff[BUFFER_SIZE + 1];
    struct sockaddr_in sin;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(40713);

    if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket");
        return 1;
    }

    if(connect(fd, (struct sockaddr *)& sin, sizeof(sin)) < 0){
        perror("connect");
        return 1;
    }

    char *send_msg = "Hello\n";
    if(send(fd, send_msg, strlen(send_msg), 0) != strlen(send_msg)){
        perror("miss soem data");
    }

    int bytes;
    if((bytes=recv(fd, buff, BUFFER_SIZE, 0)) > 0){
        buff[bytes] = '\0';
        printf("%s\n", buff);
    }
    close(fd);
    return 0;
}

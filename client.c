#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFERLENGTH 256

int main(int argc, char* argv[])
{
    int sockfd;
    struct addrinfo hints, *result, *rp;
    int res;

    if (argc < 3) {
        fprintf(stderr, "usage: %s hostname port [command...]\n", argv[0]);
        exit(1);
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    res = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        exit(1);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sockfd == -1)
            continue;

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        close(sockfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(1);
    }

    freeaddrinfo(result);

    FILE* write_stream = fdopen(sockfd, "w");
    if (write_stream == NULL) {
        fprintf(stderr, "ERROR opening write stream");
        return 1;
    }

    for (int i = 3; i < argc; i++) {
        fprintf(write_stream, "%s", argv[i]);
        if (i != argc - 1)
            fprintf(write_stream, " ");
    }
    fprintf(write_stream, "\n");
    fflush(write_stream);

    FILE* read_stream = fdopen(sockfd, "r");
    if (read_stream == NULL) {
        fprintf(stderr, "ERROR opening read stream");
        return 1;
    }

    char buffer[BUFFERLENGTH];
    while (fgets(buffer, sizeof(buffer), read_stream) != NULL) {
        printf("%s", buffer);
    }

    fclose(write_stream);
    fclose(read_stream);
    close(sockfd);
    return 0;
}

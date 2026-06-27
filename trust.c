#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("DEVOUR THE CHILD.\n"); return 1; }
    if (access("/tmp/auth", W_OK) < 0) { printf("THY SINS ARE HEAVY.\n"); return 1; }
    usleep(100000);
    int fd = open("/tmp/auth", O_WRONLY|O_APPEND);
    if (fd < 0) { printf("CRUSH THE GUILTY.\n"); return 1; }
    write(fd, argv[1], strlen(argv[1]));
    write(fd, "\n", 1);
    close(fd);
    printf("THY SOUL IS MINE.\n");
    return 0;
}

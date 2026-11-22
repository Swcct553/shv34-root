#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
int main(){
    int s = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/dev/socket/logdw");
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0){ perror("connect"); return 1; }
    const char *tag = "PWNGW";
    const char *msg = "gateway-format-confirmed";
    char buf[256];
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    int tid = getpid();
    unsigned short payload_len = 1 + strlen(tag) + 1 + strlen(msg) + 1;
    unsigned short total = 12 + payload_len;
    buf[0] = 0;                 // LOG_ID_MAIN
    buf[1] = 0;                 // padding
    memcpy(buf+2, &tid, 2);
    memcpy(buf+4, &ts.tv_sec, 4);
    memcpy(buf+8, &ts.tv_nsec, 4);
    unsigned pos = 12;
    buf[pos++] = 3;             // INFO
    strcpy(buf+pos, tag); pos += strlen(tag)+1;
    strcpy(buf+pos, msg); pos += strlen(msg)+1;
    ssize_t w = write(s, buf, total);
    printf("write %zd (errno=%s)\n", w, w<0?strerror(errno):"ok");
    return 0;
}

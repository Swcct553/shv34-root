#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
int main(){
    int fd = open("/proc/self/mem", O_RDWR);
    if (fd < 0){ printf("OPEN FAIL: %s\n", strerror(errno)); return 1; }
    printf("open /proc/self/mem O_RDWR: OK\n");
    char buf[8] = "deadbeef";
    off_t addr = (off_t)(long)buf;
    if (pread(fd, buf, 4, addr) != 4){ printf("PREAD FAIL: %s\n", strerror(errno)); return 2; }
    printf("pread own stack: OK\n");
    char wbuf[8] = "c0ffee0";
    if (pwrite(fd, wbuf, 4, addr) != 4){ printf("PWRITE FAIL: %s\n", strerror(errno)); return 3; }
    printf("pwrite own stack: OK\n");
    // Now the decisive test: mmap /proc/self/exe? No - mmap a file-backed page
    int ffd = open("/proc/self/exe", O_RDONLY);
    if (ffd < 0){ printf("open exe: %s\n", strerror(errno)); return 0; }
    long sz = lseek(ffd, 0, SEEK_END);
    char *p = mmap(NULL, (size_t)((sz+4095)&~4095L), PROT_READ, MAP_PRIVATE, ffd, 0);
    if (p == MAP_FAILED){ printf("mmap: %s\n", strerror(errno)); return 0; }
    off_t paddr = (off_t)(long)p;
    if (pwrite(fd, wbuf, 4, paddr) == 4) printf("DIRTYCOW PWRITE FILE PAGE: OK\n");
    else printf("DIRTYCOW PWRITE FILE PAGE FAIL: %s\n", strerror(errno));
    return 0;
}

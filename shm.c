#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <sys/shm.h>
#include <sys/mman.h>

enum {
    NANOSECONDS_IN_MICROSECOND = 1000,
    NANOSECONDS_IN_MILLISECOND = NANOSECONDS_IN_MICROSECOND * 1000,
    NANOSECONDS_IN_SECOND = NANOSECONDS_IN_MILLISECOND * 1000
};

#define countof(a) (sizeof((a))/sizeof(*(a)))
typedef unsigned char byte;
#define null NULL
#define check(expr) { int r = (expr); if (r != 0) { perror( #expr ); exit(1); } }

static uint64_t time_in_nanoseconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NANOSECONDS_IN_SECOND + (uint64_t)ts.tv_nsec; 
}

static struct sockaddr_in localhost() {
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(5555);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {	
        perror("inet_pton() failed");
        exit(1);
    }
    return address;    
}

static byte receive_byte(int s) {
    for (;;) {
        byte b = 0;
        int r = recv(s, &b, 1, 0);
        if (r < 0) { perror("recv() failed"); exit(1); }
        if (r == 1) { return b; }
    }
}

static void send_byte(int s, byte b) {
    for (;;) {
        int r = send(s, &b, 1, MSG_DONTROUTE);
        if (r < 0) { perror("send() failed"); exit(1); }
        if (r == 1) { return; }
    }
}

static void report_mps(uint64_t* last_mps_time, int* mps) {
    (*mps)++;
    uint64_t time = time_in_nanoseconds();     
    if (time > *last_mps_time + 1LL * NANOSECONDS_IN_SECOND) {
        printf("roundtrips per second=%.1f\n", *mps / 1.0);
        *mps = 0;
        *last_mps_time = time;
    }
}

static const int ON = 1;
static int shm_fd;
static byte* shm_base;
enum { CHUNK = 4096 };

static void open_and_map_shm(bool server) {
    if (server) { shm_unlink("/shm"); }
    shm_fd = shm_open("/shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("shm_open() failed"); exit(1); }
    if (server) { check(ftruncate(shm_fd, 256 * CHUNK)); }
    shm_base = mmap(0, 256 * CHUNK, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) { perror("mmap() failed"); exit(1); }  
}

static void server() {
    open_and_map_shm(true);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    check(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&ON, sizeof(ON)));
    struct sockaddr_in address = localhost();
    check(bind(listener, (const struct sockaddr*)&address, sizeof(struct sockaddr_in)));
    check(listen(listener, 16));
    int s = accept(listener, null, 0);    
    if (s <= 0) { perror("accept() failed"); exit(1); }
    check(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&ON, sizeof(ON)));
    printf("server accepted\n");
    uint64_t last_mps_time = time_in_nanoseconds();     
    int mps = 0; // messages per second (round trip actually)    
    byte b = 0;
    for (;;) {
        memset(shm_base + b * CHUNK, b, CHUNK);
        send_byte(s, b);
//      printf("server sent: 0x%02X\n", b);
        b = receive_byte(s);
//      printf("server recv: 0x%02X\n", b);
        report_mps(&last_mps_time, &mps);
    }
}

static void client() {
    int s = socket(AF_INET, SOCK_STREAM, 0);;
    struct sockaddr_in address = localhost();
    check(connect(s, (const struct sockaddr*)&address, sizeof(address)));
    check(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&ON, sizeof(ON)));
    printf("client connected");
    open_and_map_shm(false);    
    uint64_t last_mps_time = time_in_nanoseconds();     
    int mps = 0; // messages per second (round trip actually)    
    for (;;) {
        byte b = receive_byte(s);
        bool equ = true;
        byte* p = &shm_base[b * CHUNK];
        for (int i = 0; i < CHUNK; i++) {
            equ = equ && p[i] == b;
        }
        assert(equ);
//      printf("client recv: 0x%02X\n", b);
        b++;
//      printf("client sent: 0x%02X\n", b);
        send_byte(s, b);
        report_mps(&last_mps_time, &mps);
//      adding 2 ms sleep make it: roundtrips per second=300+ with combined CPU utilization ~7%        
//      struct timespec ts = {0, 2 * NANOSECONDS_IN_MILLISECOND};
//      nanosleep(&ts, null);
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) { server(); }
    else if (argc > 1 && strcmp(argv[1], "client") == 0) { client(); }
    else { fprintf(stderr, "%s server|client\n", argv[0]); exit(1); }
    return 0;
}    
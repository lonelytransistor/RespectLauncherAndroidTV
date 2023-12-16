#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

pthread_mutex_t MAIN_MUTEX;
#define MUTEX_LOCK() pthread_mutex_lock(&MAIN_MUTEX);
#define MUTEX_UNLOCK() pthread_mutex_unlock(&MAIN_MUTEX);

int DEBUGE = 0;
int STDIN  = 1;
int STDOUT = 1;
int STDERR = 1;
void DEBUG(const char *format, ...) {
    if (STDOUT != 1 || DEBUGE != 1)
        return;
    va_list args;
    va_start(args, format);
    fprintf(stdout, "DEBUG: ");
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
}
void INFO(const char *format, ...) {
    if (STDOUT != 1)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
}
void WARN(const char *format, ...) {
    if (STDERR != 1)
        return;
    va_list args;
    va_start(args, format);
    MUTEX_LOCK();
    fprintf(stderr, "WARN:  ");
    vfprintf(stderr, format, args);
    fprintf(stdout, "\n");
    MUTEX_UNLOCK();
    va_end(args);
}
void ERROR(const char *format, ...) {
    if (STDERR != 1)
        return;
    va_list args;
    va_start(args, format);
    MUTEX_LOCK();
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, " errno = %s\n", strerror(errno));
    MUTEX_UNLOCK();
    va_end(args);
}
char* APP_NAME = NULL;
char* APP_PATH = NULL;
char* getAppPath() {
    if (APP_PATH == NULL) {
        char buf[512];
        int bufLen = readlink("/proc/self/exe", buf, 512) + 1;
        buf[bufLen-1] = 0;

        APP_PATH = (char*) malloc(bufLen);
        strcpy(APP_PATH, buf);
    }
    return APP_PATH;
}
char* getAppName() {
    getAppPath();
    if (APP_NAME == NULL) {
        int len = strlen(APP_PATH)-1;
        int ix;
        for (ix = len; APP_PATH[ix] != '/' && ix>0; ix--);
        APP_NAME = &APP_PATH[ix != len ? ix+1 : len];
    }
    return APP_NAME;
}
int atoi_checked(char* string) {
    int size = strlen(string);
    for (int ix = 0; ix < size; ix++) {
        if (string[ix] < '0' || string[ix] > '9') {
            return -1;
        }
    }
    return size>0 ? atoi(string) : -1;
}
char* strnstr(const char* haystack, const char* needle, int hLen) {
    int nLen = strlen(needle);
    for (int ix = 0; ix <= hLen-nLen; ix++) {
        if (haystack[ix] == needle[0]) {
            if (strncmp(haystack + ix, needle, nLen) == 0) {
                return (char*) (haystack + ix);
            }
        }
    }
    return NULL;
}
int fileContains(const char* path, const char* text) {
    char buf[512];
    int fd, nread;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        ERROR("open('%s') failed", path);
        return 0;
    }
    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread == -1) {
        ERROR("read(fd) failed");
        return 0;
    }
    buf[nread] = 0;
    nread = strnlen(buf, nread);
    if (nread < strlen(text) || strnstr(buf, text, nread) == NULL) {
        return 0;
    }
    return 1;
}
int killall(const char name[], const char path[]) {
    DIR *dir;
    struct dirent *entry;
    char buf[64];
    int fd, pid, nread;
    int selfPid = getpid();
    int nLen = name != NULL ? strlen(name) : 0;
    int pLen = path != NULL ? strlen(path) : 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        ERROR("opendir('/proc') failed");
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        pid = atoi_checked(entry->d_name);
        if (pid==-1 || pid==selfPid) {
            continue;
        }

        if (nLen) {
            snprintf(buf, sizeof(buf), "/proc/%d/cmdline", pid);
            fd = open(buf, O_RDONLY);
            if (fd == -1) {
                ERROR("open('%s') failed", path);
                continue;
            }
            nread = read(fd, buf, sizeof(buf) - 1);
            close(fd);

            if (nread == -1) {
                ERROR("read(fd) failed");
                continue;
            }
            buf[nread] = 0;
            nread = strnlen(buf, nread);
            if (nread < nLen || strnstr(buf, name, nread) == NULL) {
                continue;
            }
        }
        if (pLen) {
            snprintf(buf, sizeof(buf), "/proc/%d/exe", pid);
            nread = readlink(buf, buf, sizeof(buf));
            buf[nread > 0 ? nread : 0] = 0;

            if (nread < pLen || strnstr(buf, path, nread) == NULL) {
                continue;
            }
        }
        if (kill(pid, SIGKILL) == -1) {
            ERROR("kill(%d, SIGKILL) failed", pid);
        } else {
            return 0;
        }
    }
    closedir(dir);
    return -2;
}


int SERVER_PORT = 6667;
int SHELL_SERVER_PORT = 6668;
char SHELL_SERVER_IP[] = "127.0.0.1";
char SERVER_IP[] = "255.255.255.255";

struct KeyData {
    uint16_t code;
    int32_t scanCode;
    uint32_t timeDown;
};
struct KeyInfo {
    struct KeyData key;

    int state;
    struct timeval timeStamp;

    int filterKeyCodeSize;
    int filterScanCodeSize;
    uint16_t* filterKeyCode;
    int32_t* filterScanCode;
};
struct watchDevice_t;
struct watchDevice_t {
    struct KeyInfo info;
    uint8_t devNum;
    int fd_uinput;
    pthread_t threadId;
    uint8_t* buffer;
    uint16_t bufferLen;
    struct input_event ev;
    int fd;
    uint8_t grabbed;
    int sock;
    int (*signalFunc)(struct watchDevice_t*, uint16_t, uint32_t, uint32_t);
};
struct threadData {
    uint8_t keepRunning;
    pthread_t threadId;
};

int shellServer_priv(struct threadData* p) {
    char buf[512];
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock <= 0) {
        ERROR("socket(AF_INET, SOCK_STREAM, 0)");
        return -1;
    }
    struct sockaddr_in serverAddress, clientAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SHELL_SERVER_PORT);
    serverAddress.sin_addr.s_addr = inet_addr(SERVER_IP);
    if (bind(sock, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0) {
        ERROR("bind() failed");
        return -1;
    }
    while (p->keepRunning) {
        if (listen(sock, 10) == 0) {
            printf("Listening...\n");
        } else {
            ERROR("listen() failed");
            close(sock);
            return -1;
        }
        socklen_t clientAddress_sz = sizeof(clientAddress);
        int clientSock = accept(sock, (struct sockaddr*) &clientAddress, &clientAddress_sz);
        while (1) {
            int bytes_received = recv(clientSock, buf, sizeof(buf)-1, 0);
            if (bytes_received < 0) {
                ERROR("recv() failed");
                break;
            } else if (bytes_received == 0) {
                INFO("Client disconnected");
                break;
            } else {
                buf[bytes_received] = 0;
                FILE* fp = popen(buf, "r"); // "r" indicates reading mode
                if (fp == NULL) {
                    ERROR("popen() failed");
                    buf[0] = 0;
                    buf[1] = -1;
                    send(clientSock, buf, 2, 0);
                } else {
                    int ret = read(fileno(fp), buf, sizeof(buf)-2);
                    buf[ret] = 0;
                    buf[ret+1] = (int8_t) pclose(fp);
                    send(clientSock, buf, ret+2, 0);
                }
            }
        }
        close(clientSock);
    }
    close(sock);
    return 0;
}
void* shellServer_priv_entry(void* params) {
    shellServer_priv((struct threadData*) params);
    return NULL;
}
#define shellServer(a) shellServer_priv_start(a,0)
#define shellServerThread(a) shellServer_priv_start(a,1)
void shellServer_priv_start(struct threadData* p, int threaded) {
    if (threaded) {
        pthread_create(&p->threadId, NULL, shellServer_priv_entry, p);
    } else {
        shellServer_priv_entry(p);
    }
}
// Signal key press
int signalKeyPressServerDisconnect(struct watchDevice_t* p) {
    if (p->sock <= 0) {
        WARN("Invalid socket");
        return -1;
    }
    close(p->sock);
    p->sock = -1;
    return 0;
}
int signalKeyPressServerConnect(struct watchDevice_t* p) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock <= 0) {
        ERROR("socket(AF_INET, SOCK_STREAM, 0)");
        return -1;
    }
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        ERROR("connect() failed");
        close(sock);
        return -1;
    }
    p->sock = sock;
    return 0;
}
int signalKeyPressServer(struct watchDevice_t* p, uint16_t keyCode, uint32_t scanCode, uint32_t timeDown) {
    if (keyCode==0 && scanCode==0 && timeDown==0) {
        return signalKeyPressServerDisconnect(p);
    }

    if (p->sock <= 0) {
        signalKeyPressServerConnect(p);
    }
    if (p->sock <= 0) {
        WARN("Invalid socket");
        return -1;
    }
    struct KeyData key;
    key.code = htons(keyCode);
    key.scanCode = htons(scanCode);
    key.timeDown = htons(timeDown);
    int ret = send(p->sock, &key, sizeof(key), 0);
    if (ret == -1) {
        ERROR("send() failed");
        return -1;
    }
    return 0;
}
int signalKeyPressCmd(struct watchDevice_t* p, uint16_t keyCode, uint32_t scanCode, uint32_t timeDown) {
    if (keyCode==0 && scanCode==0 && timeDown==0) {
        return 0;
    }

    if (timeDown > 500) {
        //return system("am start $(dumpsys package | grep -A 9999 'com.android.systemui.TOGGLE_RECENTS' | grep -m 2 -B 9999 ':' | head -n2 | tail -n1 | grep -voE '[^ ]+ ')");
        //return system("am start -a com.android.systemui.TOGGLE_RECENTS -f 65536");
        int ret = system("am broadcast -a com.android.systemui.TOGGLE_RECENTS -f 0x01000000 | grep 'result=1' > /dev/null");
        if (ret) {
            return system("am start -a com.android.systemui.TOGGLE_RECENTS -f 65536");
        }
        return ret;
    } else if (timeDown > 0) {
        return system("am start $(cmd shortcut get-default-launcher | grep -o -e '{.*}' | grep -vo -e '[{}]')");
    }
    return 0;
}
//

// Find device
#define MAX(a, b) ((a)>(b) ? (a) : (b))
#define TESTBIT(bit, array) ((array[bit/8] & (1UL<<(bit%8))) != 0 ? 1 : 0)
#define BITMASK_SZ (MAX(KEY_MAX + 7, EV_MAX + 7)/8)
void printBitMask(uint8_t* bitMask) {
    for (int ix = 0; ix < BITMASK_SZ; ix++) {
        DEBUG("%08b ", bitMask[ix]);
    }
}
void copyBitMapsToUInput(int fd, int fd_uinput, uint8_t* bitMask) {
    if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bitMask) >= 0) {
        for (int ix = 0; ix < EV_MAX; ix++) {
            if (TESTBIT(ix, bitMask)) {
                ioctl(fd_uinput, UI_SET_EVBIT, ix);
            }
        }
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), bitMask) >= 0) {
        for (int ix = 0; ix < KEY_MAX; ix++) {
            if (TESTBIT(ix, bitMask)) {
                ioctl(fd_uinput, UI_SET_KEYBIT, ix);
            }
        }
    }
}
uint8_t isKeySupported(int fd, struct KeyInfo* info, uint8_t* bitMask) {
    if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bitMask) < 0) {
        ERROR("ioctl(0, EVIOCGBIT)");
        return 0;
    }
    if (!TESTBIT(EV_KEY, bitMask)) {
        return 0;
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), bitMask) < 0) {
        ERROR("ioctl(EV_KEY, EVIOCGBIT)");
        return 0;
    }
    for (int ix = 0; ix < info->filterKeyCodeSize; ix++) {
        if (TESTBIT(info->filterKeyCode[ix], bitMask)) {
            return 1;
        }
    }
    return 0;
}
uint8_t findEventDevice(struct KeyInfo* info, uint8_t* devices, uint8_t maxDevices, int fd_uinput) {
    uint8_t dev_ix = 0;
    uint8_t devNum = 0;
    char devicePath[20] = {0};
    uint8_t bitMask[BITMASK_SZ] = {0};
    struct dirent *entry;
    DIR* dir;

    if ((dir = opendir("/dev/input")) == NULL) {
        WARN("Error opening input directory");
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            dev_ix = atoi(entry->d_name + 5);
            snprintf(devicePath, sizeof(devicePath), "/dev/input/event%d", dev_ix);

            int fd = open(devicePath, O_RDONLY);
            if (fd == -1) {
                ERROR("open(%s, O_RDONLY)", devicePath);
                continue;
            }
            if (isKeySupported(fd, info, bitMask)) {
                devices[devNum++] = dev_ix;
                if (fd_uinput > 0) {
                    copyBitMapsToUInput(fd, fd_uinput, bitMask);
                }
                if (devNum >= maxDevices) {
                    break;
                }
            }
            close(fd);
        }
    }
    closedir(dir);
    return devNum;
}
//

// Watch one device
int8_t readEventPacket(struct watchDevice_t* p) {
    uint8_t nread;
    while (1) {
        nread = read(p->fd, &p->ev, sizeof(struct input_event));
        if (nread != sizeof(struct input_event)) {
            WARN("Size mismatch %d != %d", nread, sizeof(struct input_event));
            return -1;
        }
        DEBUG("RD:  type %d, code %d, value %d", p->ev.type, p->ev.code, p->ev.value);
        memcpy(p->buffer + p->bufferLen, &p->ev, nread);
        p->bufferLen += nread;

        switch (p->ev.type) {
            case EV_SYN:
                if (!p->grabbed) {
                    if (p->info.state == 0) {
                        ioctl(p->fd, EVIOCGRAB, 1);
                        p->grabbed = 1;
                        DEBUG("REGRAB");
                    }
                    p->bufferLen = 0;
                    return 0;
                }
                for (int ix = 0; ix < p->info.filterKeyCodeSize; ix++) {
                    if (p->info.filterKeyCode[ix] == p->info.key.code) {
                        p->bufferLen = 0;
                        return 1;
                    }
                }
                for (int ix = 0; ix < p->info.filterScanCodeSize; ix++) {
                    if ( p->info.filterScanCode[ix] == p->info.key.scanCode) {
                        p->bufferLen = 0;
                        return 1;
                    }
                }
                if (p->bufferLen <= 0) {
                    return 0;
                }
                if (DEBUGE) {
                    for (int ix = 0; ix < p->bufferLen; ix+=sizeof(struct input_event)) {
                        struct input_event* ev2 = (struct input_event*) (p->buffer + ix);
                        DEBUG("BUF: type %d, code %d, value %d", ev2->type, ev2->code, ev2->value);
                    }
                }
                if (p->fd_uinput > 0) {
                    MUTEX_LOCK();
                    write(p->fd_uinput, p->buffer, p->bufferLen);
                    MUTEX_UNLOCK();
                    p->bufferLen = 0;
                } else {
                    ioctl(p->fd, EVIOCGRAB, 0);
                    p->grabbed = 0;
                    for (int ix = 0; ix < p->bufferLen; ix+=sizeof(struct input_event)) {
                        struct input_event* ev2 = (struct input_event*) (p->buffer + ix);
                        DEBUG("type %d, code %d, value %d", ev2->type, ev2->code, ev2->value);
                        if (((struct input_event*) (p->buffer + ix))->type == EV_KEY) {
                            memcpy(&p->ev, p->buffer + ix, sizeof(struct input_event));
                            p->ev.value = p->ev.value > 0 ? 0 : 1;
                            MUTEX_LOCK();
                            write(p->fd, &p->ev, sizeof(struct input_event));
                            MUTEX_UNLOCK();
                        }
                    }
                    MUTEX_LOCK();
                    write(p->fd, p->buffer, p->bufferLen);
                    MUTEX_UNLOCK();
                    DEBUG("UNGRAB: key: %d, scan: %d, val: %d", p->info.key.code, p->info.key.scanCode, p->info.state);
                }
                return 0;
                break;
            case EV_KEY:
                if (p->info.key.code == p->ev.code && p->info.state != 0 && p->ev.value == 0) {
                    p->info.key.timeDown = (p->ev.time.tv_sec  - p->info.timeStamp.tv_sec )*1000 +
                                           (p->ev.time.tv_usec - p->info.timeStamp.tv_usec)/1000;
                } else if (p->ev.value == 1) {
                    p->info.timeStamp = p->ev.time;
                    p->info.key.timeDown = 0;
                }
                p->info.key.code = p->ev.code;
                p->info.state = p->ev.value;
                break;
            case EV_MSC:
                p->info.key.scanCode = p->ev.value;
                break;
        }
    }
    return -1;
}

int consumeDataInFD(int fd) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int ready;
    char buf[512];
    while (1) {
        ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            ERROR("select() failed");
            return -1;
        } else if (ready == 0) {
            return 0;
        } else if (read(fd, buf, sizeof(buf)) < sizeof(buf)) {
            return 0;
        }
    }
}
int watchDevice_priv(struct watchDevice_t* p) {
    int8_t captured;
    char devicePath[] = "/dev/input/eventXXX";
    sprintf(devicePath, "/dev/input/event%d", p->devNum);
    INFO("Monitoring: %s", devicePath);

    p->fd = open(devicePath, p->fd_uinput>0 ? O_RDONLY : O_RDWR);
    if (p->fd == -1) {
        ERROR("open(%s, %s)", devicePath, p->fd_uinput>0 ? "O_RDONLY" : "O_RDWR");
        return 1;
    }
    consumeDataInFD(p->fd);
    if (ioctl(p->fd, EVIOCGRAB, 1) == -1) {
        ERROR("ioctl(%s, EVIOCGRAB, 1)", devicePath);
        close(p->fd);
        return 1;
    }
    p->grabbed = 1;

    do {
        if ((captured = readEventPacket(p)) == 1) {
            if (p->info.state == 1 || p->info.state == 0) {
                INFO("keyCode: %d, scanCode: %d, state: %d, length: %d", p->info.key.code, p->info.key.scanCode, p->info.state, p->info.key.timeDown);
                MUTEX_LOCK();
                if (p->signalFunc) {
                    (*p->signalFunc)(p, p->info.key.code, p->info.key.scanCode, p->info.state ? 0 : p->info.key.timeDown);
                }
                MUTEX_UNLOCK();
            }
            if (p->info.key.timeDown > 5000) {
                break;
            }
        }
    } while (captured >= 0);

    if (ioctl(p->fd, EVIOCGRAB, 0) == -1) {
        ERROR("ioctl(%s, EVIOCGRAB, 0)", devicePath);
        close(p->fd);
        return 1;
    }
    close(p->fd);
    return 0;
}
void* watchDevice_priv_entry(void* params) {
    struct watchDevice_t* p = (struct watchDevice_t*) params;
    p->buffer = (uint8_t*) malloc(sizeof(struct input_event)*100);

    watchDevice_priv(p);

    free(p->buffer);
    return NULL;
}
#define watchDevice(a) watchDevice_priv_start(a,0)
#define watchDeviceThread(a) watchDevice_priv_start(a,1)
void watchDevice_priv_start(struct watchDevice_t* p, int threaded) {
    if (threaded) {
        pthread_create(&p->threadId, NULL, watchDevice_priv_entry, p);
    } else {
        watchDevice_priv_entry(p);
    }
}
//

int mainApp(struct KeyInfo* keyInfo, uint8_t devicesNum, int fd_uinput, int (*signalFunc)(struct watchDevice_t*, uint16_t, uint32_t, uint32_t)) {
    pthread_mutex_init(&MAIN_MUTEX, NULL);

    struct uinput_setup dev_uinput;
    if (fd_uinput) {
        fd_uinput = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_uinput == -1) {
            ERROR("open(/dev/uinput, O_WRONLY | O_NONBLOCK)");
            return -1;
        } else {
            memset(&dev_uinput, 0, sizeof(dev_uinput));
            dev_uinput.id.bustype = BUS_USB;
            dev_uinput.id.vendor = 0x1234;
            dev_uinput.id.product = 0x5678;
            strcpy(dev_uinput.name, "Example device");
        }
    } else {
        fd_uinput = -1;
    }

    uint8_t* devices = (uint8_t*) malloc(devicesNum);
    struct watchDevice_t** threads;
    threads = (struct watchDevice_t**) malloc(sizeof(threads)*devicesNum);
    devicesNum = findEventDevice(keyInfo, devices, devicesNum, fd_uinput);
    if (devicesNum == 0) {
        WARN("No devices found.");
        goto exit;
    }

    if (fd_uinput > 0) {
        if (ioctl(fd_uinput, UI_DEV_SETUP, &dev_uinput) == -1) {
            ERROR("ioctl(UI_DEV_SETUP)");
            goto exit;
        }
        if (ioctl(fd_uinput, UI_DEV_CREATE) == -1) {
            ERROR("ioctl(UI_DEV_CREATE)");
            goto exit;
        }
    }

    for (int ix = 0; ix < devicesNum; ix++) {
        struct watchDevice_t* p = (struct watchDevice_t*) malloc(sizeof(struct watchDevice_t));
        memset(p, 0, sizeof(struct watchDevice_t));
        memcpy(&p->info, keyInfo, sizeof(p->info));
        p->devNum = devices[ix];
        p->fd_uinput = fd_uinput;
        p->signalFunc = signalFunc;

        watchDeviceThread(p);
        threads[ix] = p;
    }
    for (int ix = 0; ix < devicesNum; ix++) {
        pthread_join(threads[ix]->threadId, NULL);
        if (signalFunc) {
            (*signalFunc)(threads[ix], 0, 0, 0);
        }
        free(threads[ix]);
    }

    exit:
    if (fd_uinput > 0) {
        ioctl(fd_uinput, UI_DEV_DESTROY);
        close(fd_uinput);
    }
    free(devices);
    free(threads);
    return 0;
}

// Main
void printHelp() {
    printf("Usage: %s [options]\n", getAppName());
    printf("Options:\n");
    printf("  -p <port>         Set server port number, default: 6667\n");
    printf("  -h <port>         Set shell server port number, default: 6668\n");
    printf("  -s <ipaddr>       Set server IP address, default: 127.0.0.1\n");
    printf("  -m <SERVER|SHELL> Set signaling method, default: SHELL\n");
    printf("  -K <KEY1>,...     Key code filter\n");
    printf("  -S <SCAN1>,...    Scan code filter\n");
    printf("  -v                Be verbose\n");
    printf("  -f                Don't daemonize\n");
    printf("  -k                Kill other instances\n");
    printf("  -H                Create a shell server\n");
    printf("  -t                Silent\n");
    printf("  -U                Use uinput to forward events\n");
    printf("  -u                Unlink image after successful execution\n");
    printf("  --help            Display this help message\n");
}
int main(int argc, char* argv[]) {
    struct threadData shellServerThread;
    struct KeyInfo keyInfo;
    memset(&keyInfo, 0, sizeof(keyInfo));
    snprintf(SERVER_IP, sizeof(SERVER_IP), "127.0.0.1");
    int8_t method = 1;
    int8_t killAll = 0;
    int8_t forkApp = 1;
    int8_t closeStd = 0;
    int8_t shellServer = 0;
    int8_t unlinkImage = 0;
    int useUInput = 0;
    int keyCodesNum = 0;
    int scanCodesNum = 0;

    if (argc <= 1) {
        printHelp();
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            i++;
            if (i < argc) {
                SERVER_PORT = atoi(argv[i]);
                if (SERVER_PORT < 1000 || SERVER_PORT > 9999) {
                    WARN("Invalid port number");
                    return -2;
                }
            } else {
                WARN("Missing port number");
                return -1;
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            i++;
            if (i < argc) {
                SHELL_SERVER_PORT = atoi(argv[i]);
                if (SHELL_SERVER_PORT < 1000 || SHELL_SERVER_PORT > 9999) {
                    WARN("Invalid port number");
                    return -2;
                }
            } else {
                WARN("Missing port number");
                return -1;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            i++;
            if (i < argc) {
                int ip[4] = {-1};
                char buf[] = "255.255.255.255";
                strncpy(buf, argv[i], sizeof(buf));
                char* ipPart = strtok(buf, ".");
                for (int ix = 0; ix < 4 && ipPart != NULL; ix++) {
                    ip[ix] = atoi(ipPart);
                    ipPart = strtok(NULL, ".");
                }
                for (int ix = 0; ix < 4; ix++) {
                    if (ip[ix] < 0x00 || ip[ix] > 0xFF) {
                        WARN("Invalid IP address");
                        return -2;
                    }
                }
                sprintf(SERVER_IP, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            } else {
                WARN("Missing IP address");
                return -1;
            }
        } else if (strcmp(argv[i], "-m") == 0) {
            i++;
            if (i < argc) {
                if (strcmp(argv[i], "SERVER") == 0) {
                    method = 0;
                } else if (strcmp(argv[i], "SHELL") == 0) {
                    method = 1;
                } else {
                    WARN("Invalid method");
                    return -2;
                }
            } else {
                WARN("Missing method name");
                return -1;
            }
        } else if (strcmp(argv[i], "-K") == 0) {
            i++;
            if (i < argc) {
                char *token = strtok(argv[i], ",");
                while (token != NULL) {
                    keyInfo.filterKeyCodeSize++;
                    keyInfo.filterKeyCode = (uint16_t*) realloc(keyInfo.filterKeyCode, sizeof(keyInfo.key.code)*keyInfo.filterKeyCodeSize);
                    keyInfo.filterKeyCode[keyInfo.filterKeyCodeSize - 1] = atoi(token);
                    token = strtok(NULL, ",");
                }
            } else {
                WARN("Missing keycodes");
                return -1;
            }
        } else if (strcmp(argv[i], "-S") == 0) {
            i++;
            if (i < argc) {
                char *token = strtok(argv[i], ",");
                while (token != NULL) {
                    keyInfo.filterScanCodeSize++;
                    keyInfo.filterScanCode = (int32_t*) realloc(keyInfo.filterScanCode, sizeof(keyInfo.key.code)*keyInfo.filterScanCodeSize);
                    keyInfo.filterScanCode[keyInfo.filterScanCodeSize - 1] = atoi(token);
                    token = strtok(NULL, ",");
                }
            } else {
                WARN("Missing scancodes");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            printHelp();
            return 0;
        } else if (argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (int ix = 1; ix < len; ix++) {
                switch (argv[i][ix]) {
                    case 'k':
                        killAll = 1;
                        break;
                    case 'f':
                        forkApp = 0;
                        break;
                    case 't':
                        closeStd = 1;
                        break;
                    case 'U':
                        useUInput = 1;
                        break;
                    case 'H':
                        shellServer = 1;
                        break;
                    case 'u':
                        unlinkImage = 1;
                        break;
                    case 'v':
                        DEBUGE = 1;
                        break;
                    default:
                        WARN("Unknown option %s", argv[i]);
                        printHelp();
                        return -1;
                }
            }
        } else {
            WARN("Unknown option %s", argv[i]);
            printHelp();
            return -1;
        }
    }
    if (keyInfo.filterKeyCode == 0 && keyInfo.filterScanCodeSize == 0) {
        WARN("Must declare at least one keycode/scancode filter");
        return -1;
    }

    if (killAll) {
        killall(getAppName(), getAppPath());
    }
    if (forkApp) {
        pid_t pid = fork();
        if (pid < 0) {
            ERROR("fork() failed");
            return -1;
        } else if (pid > 0) {
            INFO("fork() succeeded");
            return 0;
        }
        if (setsid() < 0) {
            ERROR("setsid() failed");
            return -2;
        }
        if (unlinkImage) {
            if (unlink(getAppPath()) == -1) {
                ERROR("unlink('%s') failed", getAppPath());
                return -3;
            }
            INFO("Executable unlinked");
        }
    }
    if (closeStd) {
        STDIN = 0;
        STDOUT = 0;
        STDERR = 0;
        //fclose(stdin);
        fclose(stdout);
        fclose(stderr);
    }


    if (shellServer) {
        shellServerThread.keepRunning = 1;
        shellServerThread(&shellServerThread);
    }
    switch (method) {
        case 0:
            mainApp(&keyInfo, 10, useUInput, signalKeyPressServer);
            break;
        case 1:
            mainApp(&keyInfo, 10, useUInput, signalKeyPressCmd);
            break;
    }
    if (shellServer) {
        shellServerThread.keepRunning = 0;
        //pthread_join(shellServerThread.threadId, NULL);
    }

    free(keyInfo.filterKeyCode);
    free(keyInfo.filterScanCode);
    return 0;
}

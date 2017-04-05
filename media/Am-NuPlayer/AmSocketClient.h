#ifndef AM_SOCKET_CLIENT_H
#define AM_SOCKET_CLIENT_H

#define SEND_LEN 1024
#define SERVER_PORT 10100

int socketConnect();
void socketSend(char *buf, int size);
int socketRecv(char *buf, int size);
void socketDisconnect();
void sendTime(int64_t timeUs);

namespace android {

int writeSysfs(const char *path, char *value);
int readSysfs(const char *path, char *value);
void setPcrscr(int64_t timeUs);

}

#endif

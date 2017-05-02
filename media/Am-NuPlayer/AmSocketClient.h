#ifndef AM_SOCKET_CLIENT_H
#define AM_SOCKET_CLIENT_H

namespace android {
//------------------------------------------------------------------------------
#define SEND_LEN 1024
#define SERVER_PORT 10100

class AmSocketClient
{
public:
    AmSocketClient();
    ~AmSocketClient();
    int socketConnect();
    void socketSend(char *buf, int size);
    int socketRecv(char *buf, int size);
    void socketDisconnect();
private:
    int mSockFd;
};
//------------------------------------------------------------------------------
}
#endif

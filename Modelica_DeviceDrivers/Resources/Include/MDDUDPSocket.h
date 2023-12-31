/** UDP socket support (header-only library).
 *
 * @file
 * @author tbellmann (Windows)
 * @author bernhard-thiele (Linux)
 * @author tbeu
 * @since 2012-05-29
 * @copyright see accompanying file LICENSE_Modelica_DeviceDrivers.txt
 *
 * @note Linux version: Using recvfrom(..) seems to be tricky, especially
 * in mixed 64 and 32 bit environments. Had the problem that (sporadically!) an
 * "Invalid Argument" error was generated. Fixed it, but not sure whether it is now
 * used really correct.
 * If problems persist consider passing "NULL" as the second last argument. In this
 * case "sa_len" is ignored and the address of sender will not be provided by
 * recvfrom(..). Since it is not really needed, one could live with that.
 *
 */

#ifndef MDDUDPSocket_H_
#define MDDUDPSocket_H_

#if !defined(ITI_COMP_SIM)

#include "ModelicaUtilities.h"
#include "MDDSerialPackager.h"

#if defined(_MSC_VER) || defined(__MINGW32__)

#include <ws2tcpip.h>
#include "../src/include/CompatibilityDefs.h"

#pragma comment( lib, "Ws2_32.lib" )

#if defined(__MINGW32__)
/* This is an (ugly) fix if MinGW headers miss definitions for WSAPoll etc. */
#ifndef POLLIN
typedef struct pollfd {
    SOCKET fd;
    SHORT events;
    SHORT revents;
} WSAPOLLFD, *PWSAPOLLFD, FAR *LPWSAPOLLFD;
WINSOCK_API_LINKAGE int WSAAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, INT timeout);
#define POLLIN 768
#define POLLHUP 2
#endif /* POLLIN */
#endif /* defined(__MINGW32__) */

typedef struct MDDUDPSocket_s MDDUDPSocket;

struct MDDUDPSocket_s {
    char * receiveBuffer;
    int bufferSize;
    SOCKET SocketID;
    int useReceiveThread;
    int receiving;
    int nReceivedBytes;
    int nRecvbufOverwrites; /* Accumulated number of times new data was received without having been read out (retrieved) by Modelica */
    HANDLE hThread;
    CRITICAL_SECTION receiveLock;
};

DWORD WINAPI MDD_udpReceivingThread(LPVOID pUdp) {
    MDDUDPSocket * udp = (MDDUDPSocket *)pUdp;
    WSAPOLLFD sock_poll = {0};

    sock_poll.fd = udp->SocketID;
    sock_poll.events = POLLIN;

    while (udp->receiving == 1) {
        int ret = WSAPoll(&sock_poll, 1, 100);

        switch (ret) {
            case SOCKET_ERROR:
                {
#ifndef ITI_MDD
                    ModelicaFormatMessage("MDDUDPSocket.h: WSAPoll failed with error code: %d\n", WSAGetLastError());
#endif
                    ExitThread(1);
                }
                break;
            case 0: /* no new data available. Just check if udp->receiving still true and go on */
                break;
            default:
                if (sock_poll.revents & POLLHUP) {
#ifndef ITI_MDD
                    ModelicaFormatMessage("MDDUDPSocket.h: The UDP socket was disconnected.\n");
#endif
                }
                else {
                    BOOL socketError;
                    SOCKADDR remoteAddr;
                    int remoteAddrLen = sizeof(SOCKADDR);
                    /* Lock access to udp->receiveBuffer  */
                    EnterCriticalSection(&udp->receiveLock);
                    if (udp->nReceivedBytes > 0) (udp->nRecvbufOverwrites)++;
                    /* Receive the next datagram */
                    udp->nReceivedBytes = recvfrom(udp->SocketID, udp->receiveBuffer, udp->bufferSize, 0, &remoteAddr, &remoteAddrLen);
                    socketError = udp->nReceivedBytes == SOCKET_ERROR;
                    LeaveCriticalSection(&udp->receiveLock);
                    if (socketError) {
#ifndef ITI_MDD
                        ModelicaMessage("MDDUDPSocket.h: Receiving not possible, socket not valid.\n");
#endif
                        ExitThread(1);
                    }
                }
                break;
        }
    }
    return 0;
}

DllExport void * MDD_udpConstructor(int port, int bufferSize, int useReceiveThread) {

    int rc; /* Error variable */
    WSADATA wsa;
    SOCKADDR_IN addr;
    DWORD id1;
    MDDUDPSocket * udp;

    rc = WSAStartup(MAKEWORD(2,2),&wsa);
    if (rc != NO_ERROR) {
        ModelicaFormatError("MDDUDPSocket.h: WSAStartup failed with error code: %d\n", rc);
        return NULL;
    }

    udp = (MDDUDPSocket *)calloc(sizeof(MDDUDPSocket), 1);
    udp->SocketID = socket(AF_INET,SOCK_DGRAM,0);
    if (udp->SocketID == INVALID_SOCKET) {
        free(udp);
        udp = NULL;
        rc = WSAGetLastError();
        WSACleanup();
        ModelicaFormatError("MDDUDPSocket.h: socket failed with error: %d\n", rc);
        return udp;
    }
    udp->useReceiveThread = useReceiveThread;
    udp->receiving = 0;
    udp->bufferSize = bufferSize;
    udp->nReceivedBytes = 0;
    udp->nRecvbufOverwrites = 0;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (port) {
        rc = bind(udp->SocketID,(SOCKADDR*)&addr,sizeof(SOCKADDR_IN));
        if (rc == INVALID_SOCKET) {
            closesocket(udp->SocketID);
            free(udp);
            udp = NULL;
            rc = WSAGetLastError();
            WSACleanup();
            ModelicaFormatError("MDDUDPSocket.h: bind to port %d failed with error code: %d\n", port, rc);
            return udp;
        }
        udp->receiveBuffer = (char*)calloc(bufferSize, 1);
        InitializeCriticalSection(&udp->receiveLock);
        if (udp->useReceiveThread) {
            udp->receiving = 1;
            udp->hThread = CreateThread(0, 1024, MDD_udpReceivingThread, udp, 0, &id1);
            if (!udp->hThread) {
                DWORD dw = GetLastError();
                udp->receiving = 0;
                rc = shutdown(udp->SocketID, 2);
                if (rc == SOCKET_ERROR) {
                    ModelicaFormatMessage("MDDUDPSocket.h: shutdown failed: %d\n", WSAGetLastError());
                }
                closesocket(udp->SocketID);
                DeleteCriticalSection(&udp->receiveLock);
                free(udp->receiveBuffer);
                free(udp);
                udp = NULL;
                WSACleanup();
                ModelicaFormatError("MDDUDPSocket.h: Error creating UDP receiver thread: %lu\n", dw);
            }
        }
        ModelicaFormatMessage("MDDUDPSocket.h: Waiting for data on port %d.\n", port);
    }
    else {
        ModelicaMessage("MDDUDPSocket.h: Opened socket for sending.\n");
    }
    return (void *) udp;
}

DllExport void MDD_udpDestructor(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    if (udp) {
        int rc;
        udp->receiving = 0;
        rc = shutdown(udp->SocketID, 2);
        if (rc == SOCKET_ERROR) {
            ModelicaFormatMessage("MDDUDPSocket.h: shutdown failed with error code: %d\n", WSAGetLastError());
        }
        closesocket(udp->SocketID);
        if (udp->useReceiveThread && udp->hThread) {
            DWORD dwEc = 1;
            WaitForSingleObject(udp->hThread, 1000);
            if (GetExitCodeThread(udp->hThread, &dwEc) && dwEc == STILL_ACTIVE) {
                TerminateThread(udp->hThread, 1);
            }
            CloseHandle(udp->hThread);
            DeleteCriticalSection(&udp->receiveLock);
            free(udp->receiveBuffer);
        }
        free(udp);
    }
    WSACleanup();
}

DllExport void MDD_udpSend(void * p_udp, const char * ipAddress, int port,
                           const char * data, int dataSize) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    if (udp) {
        int rc;
        SOCKADDR_IN addr;
        addr.sin_family=AF_INET;
        addr.sin_port=htons((u_short)port);
        rc = inet_pton(AF_INET, ipAddress, &addr.sin_addr.s_addr);
        if (rc == 0) {
            ModelicaFormatError("MDDUDPSocket.h: inet_pton failed for \"%s\"\n", ipAddress);
        }
        else if (rc == -1) {
            ModelicaFormatError("MDDUDPSocket.h: inet_pton failed with error code: %d\n", WSAGetLastError());
        }
        rc = sendto(udp->SocketID, data, dataSize, 0, (SOCKADDR*)&addr, sizeof(SOCKADDR_IN));
        if (rc == SOCKET_ERROR) {
            ModelicaFormatError("MDDUDPSocket.h: sendto failed with error code: %d\n", WSAGetLastError());
        }
        else if (rc < dataSize) {
            ModelicaFormatMessage("MDDUDPSocket.h: Expected to send: %d bytes, but was: %d\n",
                                  dataSize, rc);
        }
    }
}

DllExport void MDD_udpSendP(void * p_udp, const char * ipAddress, int port,
                            void* p_package, int dataSize) {
    MDD_udpSend(p_udp, ipAddress, port, MDD_SerialPackagerGetData(p_package), dataSize);
}

DllExport const char * MDD_udpRead(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;

    if (udp && (!udp->useReceiveThread || (udp->useReceiveThread && udp->hThread))) {
        char* udpBuf;

        if (!udp->useReceiveThread) {
            SOCKADDR remoteAddr;
            int remoteAddrLen;
            remoteAddrLen = sizeof(SOCKADDR);
            udp->nReceivedBytes = recvfrom(udp->SocketID, udp->receiveBuffer, udp->bufferSize,0,&remoteAddr,&remoteAddrLen);
            if(udp->nReceivedBytes==SOCKET_ERROR) {
                ModelicaFormatMessage("MDDUDPSocket.h: Receiving not possible, socket not valid.\n");
            }
        }

        EnterCriticalSection(&udp->receiveLock);
        udpBuf = ModelicaAllocateStringWithErrorReturn(udp->bufferSize);
        if (udpBuf) {
            memcpy(udpBuf, udp->receiveBuffer, udp->nReceivedBytes);
            udp->nReceivedBytes = 0;
            LeaveCriticalSection(&udp->receiveLock);
            return (const char*) udpBuf;
        }
        else {
            LeaveCriticalSection(&udp->receiveLock);
            ModelicaError("MDDUDPSocket.h: ModelicaAllocateString failed\n");
        }
    }

    return "";
}


DllExport void MDD_udpReadP2(void * p_udp, void* p_package, int* nReceivedBytes, int* nRecvbufOverwrites) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;

    if (udp && (!udp->useReceiveThread || (udp->useReceiveThread && udp->hThread))) {
        int rc;

        if (!udp->useReceiveThread) {
            SOCKADDR remoteAddr;
            int remoteAddrLen;
            remoteAddrLen = sizeof(SOCKADDR);
            udp->nReceivedBytes = recvfrom(udp->SocketID, udp->receiveBuffer, udp->bufferSize,0,&remoteAddr,&remoteAddrLen);
            if(udp->nReceivedBytes==SOCKET_ERROR) {
                ModelicaFormatMessage("MDDUDPSocket.h: Receiving not possible, socket not valid.\n");
            }
        }

        EnterCriticalSection(&udp->receiveLock);
        rc = MDD_SerialPackagerSetDataWithErrorReturn(p_package, udp->receiveBuffer, udp->nReceivedBytes);
        *nReceivedBytes = udp->nReceivedBytes;
        *nRecvbufOverwrites = udp->nRecvbufOverwrites;
        udp->nReceivedBytes = 0;
        LeaveCriticalSection(&udp->receiveLock);
        if (rc) {
           ModelicaError("MDDUDPSocket.h: MDD_SerialPackagerSetData failed. Buffer overflow.\n");
        }
    }
}

/* Deprecated function superseded by MDD_udpReadP2 */
DllExport void MDD_udpReadP(void * p_udp, void* p_package) {
  int dummy1, dummy2;
  MDD_udpReadP2(p_udp, p_package, &dummy1, &dummy2);
}

DllExport int MDD_udpGetReceivedBytes(void * p_udp) {
    int nReceivedBytes = 0;
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;

    if (udp && udp->useReceiveThread && udp->hThread) {
        EnterCriticalSection(&udp->receiveLock);
        nReceivedBytes = udp->nReceivedBytes;
        LeaveCriticalSection(&udp->receiveLock);
    }
    return nReceivedBytes;
}

#elif defined(__linux__) || defined(__CYGWIN__)

#include <stdlib.h>
#include <string.h> /* memset(..) */
#include <errno.h>
#include <unistd.h> /* close */

#include <sys/poll.h>
#include <netdb.h>
#include <pthread.h>
#include "../src/include/CompatibilityDefs.h"

struct hostent *hostlist;   /* List of hosts returned
                   by gethostbyname. */

typedef struct MDDUDPSocket_s MDDUDPSocket;

/** UDP socket object */
struct MDDUDPSocket_s {
    int sock;  /**< connection socket. */
    struct sockaddr_in sa;   /**< server address.*/
    /* int socketMode; */  /**< Mode of socket, e.g. sender or receiver */
    size_t messageLength; /**< message length (only relevant for read socket) */
    void* msgInternal;  /**< Internal UDP message buffer (only relevant for read socket) */
    ssize_t nReceivedBytes; /**< Number of received bytes (only relevant for read socket) */
    int nRecvbufOverwrites; /**< Accumulated number of times new data was received without having been read out (retrieved) by Modelica */
    int useReceiveThread; /**< true, create dedicated receive thread and do not block main thread for incoming data (latest available data is used) */
    int runReceive; /**< Run receiving thread as long as runReceive != 0  */
    pthread_t thread;
    pthread_mutex_t messageMutex; /**< Exclusive access to message buffer */
};

/** Dedicated thread for receiving UDP messages.
 *
 * @param p_udp pointer address to the udp socket data structure
 */
void* MDD_udpReceivingThread(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    socklen_t sa_len = sizeof(struct sockaddr_in);  /*  Size of sa. */
    struct pollfd sock_poll;
    int ret;

    ModelicaFormatMessage("Started dedicated UDP receiving thread listening at port %d\n",
                          udp->sock);

    sock_poll.fd = udp->sock;
    sock_poll.events = POLLIN;

    while (udp->runReceive) {
        ret = poll(&sock_poll, 1, 100);

        switch (ret) {
            case -1:
                ModelicaFormatError("MDDUDPSocket.h: poll(..) failed (%s) \n",
                                    strerror(errno));
                break;
            case 0: /* no new data available. Just check if udp->runReceive still true and go on */
                break;
            case 1: /* new data available */
                if (sock_poll.revents & POLLHUP) {
                    ModelicaMessage("MDDUDPSocket.h: The UDP socket was disconnected.\n");
                }
                else {
                    /* Lock access to udp->msgInternal  */
                    pthread_mutex_lock(&(udp->messageMutex));
                    if (udp->nReceivedBytes > 0) (udp->nRecvbufOverwrites)++;
                    /* Receive the next datagram  */
                    udp->nReceivedBytes =
                        recvfrom(udp->sock,                    /* UDP socket */
                                 udp->msgInternal,             /* receive buffer */
                                 udp->messageLength,           /* max bytes to receive */
                                 0,                            /* no special flags */
                                 (struct sockaddr*) &(udp->sa),/* address of sender */
                                 &sa_len
                                );
                    if (udp->nReceivedBytes < 0) {
                        pthread_mutex_unlock(&(udp->messageMutex));
                        ModelicaFormatError("MDDUDPSocket.h: recvfrom(..) failed (%s)\n",
                                            strerror(errno));
                    }
                    else {
                        pthread_mutex_unlock(&(udp->messageMutex));
                    }
                }
                break;
            default:
                ModelicaFormatError("MDDUDPSocket.h: Poll returned %d. That should not happen.\n", ret);
        }
    }
    return NULL;
}

/** Blocking UDP receive.
 *
 * @param p_udp pointer address to the udp socket data structure
 */
int MDD_udpBlockingReceive(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    socklen_t sa_len = sizeof(struct sockaddr_in);  /*  Size of sa. */
    /* Receive the next datagram  */
	udp->nReceivedBytes =
		recvfrom(udp->sock,                   /* UDP socket */
		udp->msgInternal,             /* receive buffer */
		udp->messageLength,           /* max bytes to receive */
		0,                            /* no special flags */
		(struct sockaddr*) &(udp->sa),/* sender’s address */
		&sa_len
		);

	if (udp->nReceivedBytes < 0)
	{
		ModelicaFormatError("MDDUDPSocket.h: recfrom(..) failed (%s)\n",
			strerror(errno));
	}
	return 0;
}

/** Read data from UDP socket.
 *
 * @param p_udp pointer address to the udp socket data structure
 * @return pointer to the message buffer
 */
const char * MDD_udpRead(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    char* udpBuf;

    if(!udp->useReceiveThread) {
		 MDD_udpBlockingReceive(p_udp);
     }

    /* Lock access to udp->msgInternal  */
    pthread_mutex_lock(&(udp->messageMutex));
    udpBuf = ModelicaAllocateStringWithErrorReturn(udp->messageLength);
    if (udpBuf) {
        memcpy(udpBuf, udp->msgInternal, udp->messageLength);
        udp->nReceivedBytes = 0;
        pthread_mutex_unlock(&(udp->messageMutex));
        return (const char*) udpBuf;
    }
    else {
        pthread_mutex_unlock(&(udp->messageMutex));
        ModelicaError("MDDUDPSocket.h: ModelicaAllocateString failed\n");
    }
    return "";
}

/** Read data from UDP socket.
 *
 * @param p_udp Pointer address to the udp socket data structure
 * @param [out] nReceivedBytes Number of received bytes
 * @param [out] nRecvbufOverwrites  Accumulated number of times new data was received without having been read out (retrieved) by Modelica
 * @param p_package Pointer to the SerialPackager
 */
void MDD_udpReadP2(void * p_udp, void* p_package, int* nReceivedBytes, int* nRecvbufOverwrites) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    int rc;

    if(!udp->useReceiveThread) {
		 MDD_udpBlockingReceive(p_udp);
     }

    /* Lock access to udp->msgInternal  */
    pthread_mutex_lock(&(udp->messageMutex));
    rc = MDD_SerialPackagerSetDataWithErrorReturn(p_package, (const char*)udp->msgInternal, udp->messageLength);
    *nReceivedBytes = udp->nReceivedBytes;
    *nRecvbufOverwrites = udp->nRecvbufOverwrites;
    udp->nReceivedBytes = 0;
    pthread_mutex_unlock(&(udp->messageMutex));
    if (rc) {
        ModelicaError("MDDUDPSocket.h: MDD_SerialPackagerSetData failed. Buffer overflow.\n");
    }
}

/** @deprecated function superseded by MDD_udpReadP2 */
void MDD_udpReadP(void * p_udp, void* p_package) {
  int dummy1, dummy2;
  MDD_udpReadP2(p_udp, p_package, &dummy1, &dummy2);
}


/** Nonblocking read data from UDP socket.
 *
 * @note No Modelica interface for this function, yet.
 *
 * @param p_udp pointer address to the udp socket data structure
 * @return pointer to the message buffer
 */
const char * MDD_udpNonBlockingRead(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    socklen_t sa_len = sizeof(struct sockaddr_in);  /*  Size of sa. */
    struct pollfd sock_poll;
    int ret;

    sock_poll.fd = udp->sock;
    sock_poll.events = POLLIN;

    ret = poll(&sock_poll, 1, 0);

    switch (ret) {

        case -1:
            ModelicaFormatError("MDDUDPSocket.h: poll(..) failed (%s) \n",
                                strerror(errno));
            break;
        case 0: /* no new data available */
            ModelicaMessage("No new data at socket available\n");
            break;
        case 1: /* new data available */
            if (sock_poll.revents & POLLHUP) {
                ModelicaFormatError("MDDUDPSocket.h: The UDP socket was disconnected. Exiting.\n");
            }
            else {
                char* udpBuf = ModelicaAllocateString(udp->messageLength);
                if (udpBuf) {
                    /* Receive the next datagram. */
                    udp->nReceivedBytes =
                        recvfrom(udp->sock,                    /* UDP socket */
                                 udpBuf,                       /* receive buffer */
                                 udp->messageLength,           /* max bytes to receive */
                                 0,                            /* no special flags */
                                 (struct sockaddr*) &(udp->sa),/* address of sender */
                                 &sa_len
                                );

                    if (udp->nReceivedBytes < 0) {
                        ModelicaFormatError("MDDUDPSocket.h: recvfrom(..) failed (%s)\n",
                                            strerror(errno));
                    }
                    return (const char*) udpBuf;
                }
            }
            break;
        default:
            ModelicaFormatError("MDDUDPSocket.h: Poll returned %d. That should not happen.\n", ret);
    }

    return "";
}

/** Nonblocking read data from UDP socket.
 *
 * @note No Modelica interface for this function, yet.
 *
 * @param p_udp pointer address to the udp socket data structure
 * @param p_package pointer to the SerialPackager
 */
void MDD_udpNonBlockingReadP(void * p_udp, void* p_package) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    socklen_t sa_len = sizeof(struct sockaddr_in);  /*  Size of sa. */
    struct pollfd sock_poll;
    int ret;
    int rc;

    sock_poll.fd = udp->sock;
    sock_poll.events = POLLIN;

    ret = poll(&sock_poll, 1, 0);

    switch (ret) {
        case -1:
            ModelicaFormatError("MDDUDPSocket.h: poll(..) failed (%s) \n",
                                strerror(errno));
            break;
        case 0: /* no new data available */
            ModelicaMessage("No new data at socket available\n");
            break;
        case 1: /* new data available */
            if (sock_poll.revents & POLLHUP) {
                ModelicaFormatError("MDDUDPSocket.h: The UDP socket was disconnected. Exiting.\n");
            }
            else {
                /* Receive the next datagram. */
                udp->nReceivedBytes =
                       recvfrom(udp->sock,                    /* UDP socket */
                                udp->msgInternal,             /* receive buffer */
                                udp->messageLength,           /* max bytes to receive */
                                0,                            /* no special flags */
                                (struct sockaddr*) &(udp->sa),/* address of sender */
                                &sa_len
                            );
                if (udp->nReceivedBytes < 0) {
                    ModelicaFormatError("MDDUDPSocket.h: recvfrom(..) failed (%s)\n",
                                        strerror(errno));
                }
                rc = MDD_SerialPackagerSetDataWithErrorReturn(p_package, (const char*) udp->msgInternal, udp->nReceivedBytes);
                if (rc) {
                    ModelicaError("MDDUDPSocket.h: MDD_SerialPackagerSetData failed. Buffer overflow.\n");
                }
            }
            break;
        default:
            ModelicaFormatError("MDDUDPSocket.h: Poll returned %d. That should not happen.\n", ret);
    }
}

/** Sent data via UDP socket.
 * @todo Information about ipAddress and port seems to be better suited to be given in the
 *       MDD_udpCreateSocket(..) functions for performance reasons, e.g., extensive tests about
 *       validity of passed in address and so on could be done once and for all time.
 * @param p_udp pointer address to the udp socket data structure
 * @param ipAddress (Remote) IP address to connect to
 * @param port (Remote) Port to connect to
 * @param data pointer to data that should be sent
 * @param dataSize size of message to be sent in byte
 */
void MDD_udpSend(void * p_udp, const char * ipAddress, int port,
                 const char * data, int dataSize) {

    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;

    int ret;
    /* Buffer for converting the resolved address to a readable format. */
    /* char dotted_ip[15]; */

    /* Look up the hostname with DNS. gethostbyname
       (at least most UNIX versions of it) properly
       handles dotted IP addresses as well as hostnames. */
    /*util_debug("Looking up %s...\n", ipAddress);*/
    hostlist = gethostbyname(ipAddress);
    if (hostlist == NULL) {
        ModelicaFormatError("MDDUDPSocket.h: gethostbyname(..) failed. Unable to resolve %s.\n", ipAddress);
    }

    /* Good, we have an address. However, some sites
       are moving over to IPv6 (the newer version of
       IP), and we are not ready for it (since it uses
       a new address format). It is a good idea to check
       for this. */
    if (hostlist->h_addrtype != AF_INET) {
        ModelicaFormatError("MDDUDPSocket.h: Error, %s does not seem to be an IPv4 address.\n",
                            ipAddress);
    }

    /* inet_ntop converts a 32-bit IP address to
       the dotted string notation (suitable for printing).
       hostlist->h_addr_list is an array of possible addresses
       (in case a name resolves to more than one IP). In most
       cases we just want the first. */
    /* inet_ntop(AF_INET, hostlist->h_addr_list[0], dotted_ip, 15);
       util_debug("Resolved %s to %s.\n", ipAddress, dotted_ip); */

    /* need to convert the target port number with the htons macro
       to network byte order */
    udp->sa.sin_port = htons(port);
    /* The IP address is already
       in network byte order (from the gethostbyname call)
       The IP address was returned as a char * for various reasons.
       Just memcpy it into the sockaddr_in structure. */
    memcpy(&(udp->sa.sin_addr), hostlist->h_addr_list[0],
           hostlist->h_length);

    /* ModelicaFormatMessage("udp->sock: %d data: %s\n dataSize: %d\n",udp, data, dataSize); */

    ret = sendto(udp->sock,          /* initialized UDP socket */
                 data,               /* data to send */
                 dataSize,           /* message length */
                 0,                  /* no special flags */
                 (struct sockaddr*) &(udp->sa),  /* destination */
                 sizeof(struct sockaddr_in));
    if (ret == -1) {
        ModelicaFormatError("MDDUDPSocket.h: sendto failed with error: %s\n", strerror(errno));
    }
    else if (ret < dataSize) {
        ModelicaFormatMessage("MDDUDPSocket.h: Expected to send: %d bytes, but was: %d\n",
                              dataSize, ret);
    }
}

/** Send data via UDP socket.
 * @todo Information about ipAddress and port seems to be better suited to be given in the
 *       MDD_udpCreateSocket(..) functions for performance reasons, e.g., extensive tests about
 *       validity of passed in address and so on could be done once and for all time.
 * @param p_udp pointer address to the udp socket data structure
 * @param ipAddress (Remote) IP address to connect to
 * @param port (Remote) Port to connect to
 * @param p_package pointer to the SerialPackager
 * @param dataSize size of message to be sent in byte
 */
void MDD_udpSendP(void * p_udp, const char * ipAddress, int port,
                  void* p_package, int dataSize) {
    MDD_udpSend(p_udp, ipAddress, port, MDD_SerialPackagerGetData(p_package), dataSize);
}

int MDD_udpGetReceivedBytes(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;
    int nReceivedBytes;
    pthread_mutex_lock(&(udp->messageMutex));
    nReceivedBytes = udp->nReceivedBytes;
    pthread_mutex_unlock(&(udp->messageMutex));
    return nReceivedBytes;
}

/** Create a UDP socket.
 * @todo Redesign in order to properly specify what kind of socket (receiving or sending)
 *       should be created and everything that can be checked once (e.g., validity of
 *       IP addresses) should be done here.
 * @param port @arg 0 if a sending socket shall be generated,
 *             @arg otherwise the number of the port at which the socket shall listen.
 * @param bufferSize size of the buffer used by a receiving socket (not needed for sending socket, i.e., can be set to 0)
 * @param useReceiveThread true, dedicated receiving thread writes package into shared buffer
 */
void * MDD_udpConstructor(int port, int bufferSize, int useReceiveThread) {
    MDDUDPSocket* udp = (MDDUDPSocket*) malloc(sizeof(MDDUDPSocket));
    int ret;

    udp->messageLength = bufferSize;
    udp->nReceivedBytes = 0;
    udp->nRecvbufOverwrites = 0;
    udp->useReceiveThread = useReceiveThread;
    udp->runReceive = 0;
    udp->msgInternal = calloc(udp->messageLength,1);
    ret = pthread_mutex_init(&(udp->messageMutex), NULL); /* Init mutex with defaults */
    if (ret != 0) {
        ModelicaFormatError("MDDUDPSocket.h: pthread_mutex_init() failed (%s)\n",
                            strerror(errno));
    }

    /* Create a SOCK_DGRAM socket. */
    udp->sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp->sock < 0) {
        ModelicaFormatError("MDDUDPSocket.h: socket(..) failed (%s)\n",
                            strerror(errno));
    }
    ModelicaFormatMessage("Created socket handle: %d (%s socket)\n",
                          udp->sock, port == 0 ? "sending" : "receiving");

    /* Zero out the entire sockaddr_in structure. */
    memset(&(udp->sa), 0, sizeof(struct sockaddr_in));

    /* This is an Internet socket. */
    udp->sa.sin_family = AF_INET;

    /* We have a different setup, depending on the chosen mode (which depends on the value
     * of the port argument */
    if (port) {
        ModelicaFormatMessage("Binding receiving UDP socket to port %d ...\n", port);
        /* need to convert the listening port number with the htons macro
           to network byte order */
        udp->sa.sin_port = htons(port);
        udp->sa.sin_addr.s_addr = htonl(INADDR_ANY);  /* listen on
                                 all interfaces */

        /* Bind to a port so the networking software will know
           which port we are interested in receiving packets from. */
        if (bind(udp->sock, (struct sockaddr *)&(udp->sa),
                 sizeof(udp->sa)) < 0) {
            ModelicaFormatError("MDDUDPSocket.h: bind(..) failed (%s)\n",
                                strerror(errno));
        }

        if (udp->useReceiveThread) {
            /* Start dedicated receiver thread */
            udp->runReceive = 1;
            ret = pthread_create(&udp->thread, 0, &MDD_udpReceivingThread, udp);
            if (ret) {
                ModelicaFormatError("MDDUDPSocket: pthread_create(..) failed\n");
            }
        }
    }

    return (void *) udp;
}

/** Close socket and free memory.
 *  @param p_udp pointer address to the udp socket data structure
 */
void MDD_udpDestructor(void * p_udp) {
    MDDUDPSocket * udp = (MDDUDPSocket *) p_udp;

    /* stop receiving thread if any */
    if (udp->runReceive) {
        void * pRet;
        udp->runReceive = 0;
        pthread_join(udp->thread, &pRet);
        pthread_detach(udp->thread);
    }

    if (pthread_mutex_destroy(&(udp->messageMutex)) != 0) {
        ModelicaFormatMessage("MDDUDPSocket.h: pthread_mutex_destroy() failed (%s)\n", strerror(errno));
    }

    if (close(udp->sock) == -1) {
        ModelicaFormatMessage("MDDUDPSocket.h: close() failed (%s)\n", strerror(errno));
    }
    ModelicaFormatMessage("Closed UDP socket with socket handle %d\n", udp->sock);

    free(udp->msgInternal);
    free(udp);
}

#else

#error "Modelica_DeviceDrivers: No support of UDPSocket for your platform"

#endif /* defined(_MSC_VER) */

#endif /* !defined(ITI_COMP_SIM) */

#endif /* MDDUDPSocket_H_ */

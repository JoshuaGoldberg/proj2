#ifndef _PROJ2_H
#define _PROJ2_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

void * markerSendThread(void *info);
int setupFromFile(int argc, char *argv[]);
int setupSocket();
int sendToken();
int snapshotHandler(int idbinding, int receivedSnapId, int i);

#endif
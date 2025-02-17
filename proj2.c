#include "proj2.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>

// A bunch of global variables just to make helpers and threads less of a pain
// since the scope of the project is small
// I'll use less in the future!
char * hostfile_location;
float tokenDelay = 0.0;
int startWithToken = 0;
char tokenMsg[10] = "token";
int predID;
int succID;
int processID;
int outgoingChannels[100]; 
int incomingChannels[100];
char *hostNames[100];
int host_num = 0;
char hostName[100];
int STATE = 0;
int hasToken = 0;
int snapshotting[32];
int idbindings[32];
int idbindingsize = 0;
int snapshotState = 0;
int closedChannels[32][100];
char channelQueues[32][100][100];
float markerDelay = 0.0;
int snapshotStarter = 0;
int snapshotDelay = 0;
int snapshotID = 0;
int completedSnapshot = 0;
int recording[32];


typedef struct marker_data
{
  int snapID;
  int snapshotState;

} marker_data_t;

// thread for token sending logic
// allows us to sleep without impacting the system
// Not in use for the current code
// but may be worth a test if you want to try a version of this program
// that doesn't block snapshotting (has interesting results!)

/* 
    void *tokenSendThread(void *info) {

        usleep(tokenDelay * 1000000);

        // send to the next in the "ring"
        if (send(outgoingChannels[succID], tokenMsg, strlen(tokenMsg), 0) == -1) {
            printf("Error sending token\n");
        }    

        hasToken = 0;

    }
*/

// thread for marker sending logic
// allows us to sleep without impacting the system
void *markerSendThread(void *info) {

    marker_data_t * data = info;

    snapshotState = STATE;
    char hasTokenResult[10];
    if(hasToken == 0) {
        strcpy(hasTokenResult, "NO");
    } else {
        strcpy(hasTokenResult, "YES");
    }

    
    int idbinding = -1;

    // find the binding
    for (int j = 0; j < 32; j++) {
        
        if(idbindings[j] == data->snapID) {
            idbinding = j;
        }

    }

    // since we only want marker send to delay, we actually perform this first
    // which simplifies things
    recording[idbinding] = 1;

    // sleeping occurs in the thread to not interfere with the algorithm
    usleep(markerDelay * 1000000);

    // prepare to actually send all the cool markers
    char markerMsg[100];
    snprintf(markerMsg, sizeof(markerMsg), "marker:%d", data->snapID);

    for(int i = 1; i <= host_num; i++) {

        if(processID != i) {

            // confirms a marker is being sent for the process
            // in the console, they may show up out of order due to some network randomness
            // but they do get printed before the receiving processes print started
            fprintf(stderr, "{proc_id:%d, snapshot_id: %d, sender:%s, receiver:%s, msg:\"marker\", state:%d, has_token:%s}\n", 
                processID, data->snapID, hostNames[processID - 1],
                hostNames[i - 1], snapshotState, hasTokenResult);

            //marker inbound to another process!
            if (send(outgoingChannels[i], markerMsg, strlen(markerMsg), 0) == -1) {
                printf("Error sending marker\n");
            }

        }

    }

    free(data);
}


int setupFromFile(int argc, char *argv[]) {

    // first, we take a trip down proj1 memory lane   
    // like proj1, but we parse a few more args
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "-h") == 0) {
            hostfile_location = argv[i + 1];
        } else if(strcmp(argv[i], "-t") == 0) {
            tokenDelay = atof(argv[i + 1]);
        } else if(strcmp(argv[i], "-x") == 0) {
            hasToken = 1;
            startWithToken = 1;
        } else if(strcmp(argv[i], "-m") == 0) {
            markerDelay = atof(argv[i + 1]);
        } else if(strcmp(argv[i], "-s") == 0) {
            snapshotStarter = 1;
            snapshotDelay = atof(argv[i + 1]);
        } else if(strcmp(argv[i], "-p") == 0) {
            snapshotID = atof(argv[i + 1]);
        } 
        
    }

    // open the hostfile
    FILE * hostfile = fopen(hostfile_location, "r");
    if (hostfile == NULL) {
       perror("Error opening file");
       return -1;
    }

    // store a list of all host names
    char line[100];

    // handy way to get the hostname we will use to reference!
    // this will allow us to send a personalized message to other processes
    gethostname(hostName, sizeof(hostName));

    // limits to 100 hosts, should be fine?
    while (fgets(line, sizeof(line), hostfile) && host_num < 100) {
        // line breaks messing up the names...
        // makes sense since each host is on a new line
        line[strcspn(line, "\n")] = '\0';
        char * lineptr = (char*) malloc(strlen(line) + 1);
        strcpy(lineptr, line);
        hostNames[host_num] = lineptr;
        host_num++;
    }

    // We've stored all the hostnames, so we're good!
    fclose(hostfile);

    // grab our id
    for (int i = 0; i < host_num; i++) {
        if (strcmp(hostName, hostNames[i]) == 0) {
            processID = i + 1;
            break;
        }
    }

    //collect prev and next
    if(processID == 1) {
        predID = host_num;
    } else {
        predID = processID - 1;
    }

    if(processID == host_num) {
        succID = 1;
    } else {
        succID = processID + 1;
    }

    // update state accordingly (initial process gets a free one)
    if(startWithToken == 1) {
        STATE += 1;
    }

    // ready to broadcast startup message
    fprintf(stderr, "{proc_id: %d, state: %d, predecessor: %d, successor: %d}\n",
            processID, STATE, predID, succID);

    return 0;
}

int setupSocket() {
    int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    
    if(socket_desc < 0){
        printf("Error while creating socket\n");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000);
    // needed to set to INADDR_ANY, otherwise program would stall
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 

    if(bind(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr))<0){
        return -1;
    }

    if (listen(socket_desc, 100) < 0) {
        printf("Error while listening\n");
        return -1;
    }

    // let other processes start up with a 2 second pause
     usleep(2000000);

    //prepare outgoing channels
    // total host - 1
    for (int i = 1; i <= host_num; i++) {

        if(i != processID) {
          
            int socket_desc_outgoing;
            struct sockaddr_in server_addr_outgoing;
            
            socket_desc_outgoing = socket(AF_INET, SOCK_STREAM, 0);
            if(socket_desc_outgoing < 0){
                printf("Unable to create socket\n");
                return -1;
            }

            // had to do some online searching, but found this way from a CMU page using hostent
            // to allow a TCP channel to be easily connected 
            struct hostent *h;
            h = gethostbyname(hostNames[i - 1]);
            if (h == NULL) {
                fprintf(stderr, "No such host: %s\n", hostNames[i - 1]);
                return -1;
            }

            server_addr_outgoing.sin_family = AF_INET;
            server_addr_outgoing.sin_port = htons(2000);
            memcpy(&server_addr_outgoing.sin_addr.s_addr, h->h_addr, h->h_length);

            if(connect(socket_desc_outgoing, (struct sockaddr*)&server_addr_outgoing, sizeof(server_addr_outgoing)) < 0){
                printf("Unable to connect\n");
                return -1;
            }

            // may not be strictly necessary to keep track of the channels in order, but makes it nicer to work with
            char msg[100];
            // transfer message (int to string)
            snprintf(msg, sizeof(msg), "%d", processID);
            if (send(socket_desc_outgoing, msg, strlen(msg), 0) < 0){
                printf("Can't send\n");
                return -1;
            }

            // store the proper id for the outgoing channel
            outgoingChannels[i] = socket_desc_outgoing;
        }
    }

    // accept all possible incoming channels
    // sent message over channel allows us to identify which channel it is!
    for (int i = 1; i <= host_num - 1; i++) {

        struct sockaddr_in server_addr_incoming;
        int connection;
        int len = sizeof(server_addr_incoming); 
        connection = accept(socket_desc, (struct sockaddr *)&server_addr_incoming, &len); 
        
        if (connection < 0) { 
            printf("Server accept failed.\n"); 
            return -1;
        } 

        // by capturing this message, we can organize our channels in a nice way (useful for later)
        char msg[100];
        if (recv(connection, msg, sizeof(msg), 0) < 0){
            printf("Couldn't receive\n");
            return -1;
        }

        int converted_val = atoi(msg);

        // store the proper id for the incoming channel
        // also make sure you store for the correct one!
        incomingChannels[converted_val] = connection;
    }

    return 0;
}

int sendToken() {
    // print the send message
    fprintf(stderr, "{proc_id: %d, sender: %s, receiver: %s, message:\"token\"}\n",
            processID, hostNames[processID - 1], hostNames[succID - 1]);

    // considerable delay (maybe)
    usleep(tokenDelay * 1000000);

    // send to the next in the "ring"
    if (send(outgoingChannels[succID], tokenMsg, strlen(tokenMsg), 0) == -1) {
                printf("Error sending token\n");
    }    

    hasToken = 0; 

    // thread logic for the token (for a nonblocking version)
    // would replace the usleep, send cond, and hasToken:
    // pthread_t tokenThread;
    // pthread_create(&tokenThread, NULL, tokenSendThread, NULL);   
}

int snapshotHandler(int idbinding, int receivedSnapId, int i) {
    // now in the snapshot state
    snapshotting[idbinding] = 1;

    // print that the process is now snapshotting
    fprintf(stderr, "{proc_id:%d, snapshot_id: %d, snapshot:\"started\"}\n",
    processID, receivedSnapId);

    // close the channel
    closedChannels[idbinding][i] = 1;

    // print the channel we closed + channel queue
    fprintf(stderr, "{proc_id:%d, snapshot_id: %d, snapshot:\"channel closed\", channel:%d-%d, queue:[%s]}\n",
        processID, receivedSnapId, i, processID, channelQueues[idbinding][i]);

    // start a thread to deal with token sending
    // we do this because the thread might sleep, and we don't want it to block 
    marker_data_t *arg1 = malloc(sizeof(marker_data_t));
    arg1->snapID = receivedSnapId;
    pthread_t markerThread;
    pthread_create(&markerThread, NULL, markerSendThread, arg1);
    return 0;
}

int markerHandler(const char *msg, int i) {

    int receivedSnapId;
    sscanf(msg, "marker:%d", &receivedSnapId);    

    int idbinding = -1;

    // find the binding
    for (int j = 0; j < 32; j++) {
        
        if(idbindings[j] == receivedSnapId) {
            idbinding = j;
        }

    }

    if(idbinding == -1) {
        idbinding = idbindingsize;
        idbindings[idbindingsize] = receivedSnapId;
        idbindingsize += 1;
    }

    // check if we're in the process of a snapshot
    if(snapshotting[idbinding] == 0) {

        int snapshotHandlerRes = snapshotHandler(idbinding, receivedSnapId, i);

    } else {

        // already received a marker, just close the channel and send the message
        closedChannels[idbinding][i] = 1;
        fprintf(stderr, "{proc_id:%d, snapshot_id: %d, snapshot:\"channel closed\", channel:%d-%d, queue:[%s]}\n",
        processID, receivedSnapId, i, processID, channelQueues[idbinding][i]);

    }

    // we can now check here for snapshot completion
    int numClosed = 0;
    for(int i = 1; i <= host_num; i ++) {
        if(i != processID) {
            if(closedChannels[idbinding][i] == 1) {
                numClosed += 1;
            }
        }
    }

    // check if we've closed all other channels (n - 1)
    if(numClosed == host_num - 1) {

        // time to say goodbye to our snapshotting
        fprintf(stderr, "{proc_id:%d, snapshot_id: %d, snapshot:\"complete\"}\n",
        processID, receivedSnapId);

        // goodbye snapshotting state o7
        snapshotting[idbinding] = 0;
        recording[idbinding] = 0;

        //clear the queues and the closed channels for the next snapshot
        // make sure to only clear one of them
        for (int i = 1; i <= host_num; i++) {
            if(i != processID) {
                channelQueues[idbinding][i][0] = '\0';
                closedChannels[idbinding][i] = 0;
            }
        }

    }
}

int tokenHandler(const char * msg, int i) {

    // dealing with a token, not a marker
    // we now have the token!
    hasToken = 1;
    // record messages while snapshotting if the channel is not closed
    for (int j = 0; j < 32; j ++) {

        if(recording[j] == 1 && closedChannels[j][i] == 0) {

            if (channelQueues[j][i][0] == '\0') {
                strcat(channelQueues[j][i], msg); 
            } else {
                strcat(channelQueues[j][i], ",");
                strcat(channelQueues[j][i], msg);
            }

        }

    }

    // print the received message
    fprintf(stderr, "{proc_id: %d, sender: %s, receiver: %s, message:\"token\"}\n",
        processID, hostNames[predID - 1], hostNames[processID - 1]);
        
    STATE += 1;
    // prints the updated state
    fprintf(stderr, "{proc_id: %d, state: %d}\n", processID, STATE);

    // check if we should start a snapshot
    // checks whether its in charge of starting a snapshot,
    // whether the proper state has been reached, and whether it already performed its snapshot
    if(snapshotStarter == 1 && STATE == snapshotDelay && completedSnapshot == 0) {
        
        completedSnapshot = 1;
        snapshotting[idbindingsize] = 1;
        idbindings[idbindingsize] = snapshotID;
        idbindingsize += 1;

        // start snapshot message
        fprintf(stderr, "{proc_id:%d, snapshot_id: %d, snapshot:\"started\"}\n",
        processID, snapshotID);

        // prepare and open a thread that allows the marker logic to not block passToken
        marker_data_t *arg1 = malloc(sizeof(marker_data_t));
        arg1->snapID = snapshotID;
            
        pthread_t markerThread;
        pthread_create(&markerThread, NULL, markerSendThread, arg1);

    }
    
    // print the send message
    fprintf(stderr, "{proc_id: %d, sender: %s, receiver: %s, message:\"token\"}\n",
            processID, hostNames[processID - 1], hostNames[succID - 1]);

    // IMPORTANT NOTE
    // Originally, I had the token delay + send be in a seperate thread
    // However, this would result in cases where multiple processes would claim
    // to have the token. This didn't sit well with me, so I decided to
    // put the logic specifically for token delays (not marker delays) outside
    // of the thread. The only issue here is that while the snapshotting does
    // not block the passToken algorithm (which is good), passToken does block
    // the snapshotting, as it prevents the processeing of the marker until
    // after the token is delayed. In the end, I sent an email to Prof.Nita-Rotaru,
    // and she said that passToken blocking snapshotting should be fine. Due to this,
    // I've solidified my logic and decided to not put token passing into a thread.
    // I may keep the code around that enables it, just in case you want a version that
    // does not block the snapshoting, but gives more faulty results 
    usleep(tokenDelay * 1000000);

    // send to the next in the "ring"
    if (send(outgoingChannels[succID], tokenMsg, strlen(tokenMsg), 0) == -1) {
        printf("Error sending token\n");
    }

    hasToken = 0;
            
    // thread logic for the token (for a nonblocking version)
    // would replace the usleep, send cond, and hasToken:
    // pthread_t tokenThread;
    // pthread_create(&tokenThread, NULL, tokenSendThread, NULL);

    return 0;
}

int main(int argc, char *argv[]) {

    // quickly initialize the snapshots to 0
    for (int i = 0; i < 32; i++) {
        snapshotting[i] = 0;
        recording[i] = 0;
        idbindings[i] = -1;
    }

    // initial clearing of values
    for(int i = 1; i <= host_num; i ++) {

        for(int j = 0; j < 32; j ++) {

            if(i != processID) {
                channelQueues[j][i][0] = '\0';
                closedChannels[j][i] = 0;
            }

        }
    }

    // set up information from the hostfile
    int setupRes = setupFromFile(argc, argv);

    // establish socket connections
    int socketRes = setupSocket();
    
    // connections established, begin to have fun with the token!

    // if the process started with the token
    if (startWithToken) {

        int sendTokenResult = sendToken();
        if (sendTokenResult == -1) {
            perror("Error with token sending");
            return -1;
        }

    }

    while(1) {        

        // thanks BEEJ for the setup on select and FD, forever my GOAT
        fd_set readfds;
        FD_ZERO(&readfds);
        int max = 0;

        for (int i = 1; i <= host_num; i++) {
            if(processID != i) {

                FD_SET(incomingChannels[i], &readfds);
                if(incomingChannels[i] > max) {
                    max = incomingChannels[i];
                }
            }
        }

        // do the select
        int rv = select(max + 1, &readfds, NULL, NULL, NULL);

        if (rv == -1) {
            continue;
        }

        for(int i = 1; i <= host_num; i ++ ) {

            if(processID != i) {

                // check valid messages
                if(FD_ISSET(incomingChannels[i], &readfds)) {
                        
                        char msg[100];
                        ssize_t end = recv(incomingChannels[i], msg, sizeof(msg) - 1, 0);
                        
                        // would not work without this
                        msg[end] = '\0';

                        // check if we've received a marker
                        if(strncmp(msg, "marker:", 7) == 0) {
                            
                            int markerResult = markerHandler(&msg, i);
                            if (markerResult == -1) {
                                perror("Error with marker handling");
                                return -1;
                            }

                        } else if(strcmp(msg, tokenMsg) == 0 && i == predID) {
                            
                            int tokenResult = tokenHandler(&msg, i);
                            if (tokenResult == -1) {
                                perror("Error with token handling");
                                return -1;
                            }
                        }    
                    }

                }

            }

        }

    }



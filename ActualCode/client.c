#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

#define CONN 100
#define QUEUE_LEN 1024
#define PORT 8090
#define SHM_SIZE QUEUE_LEN*(512)+CONN*((4*2)+QUEUE_LEN+1+(32*QUEUE_LEN))+(4*4)+(3*32)+(QUEUE_LEN*32)

//---modes-----//
#define NOAUTO "NOAUTO"
#define AUTO "AUTO"

//---commands---//
#define SEND "SEND"
#define FETCH "FETCH"
#define FETCHIF "FETCHIF"
#define QUIT "QUIT"
#define EOF2 "EOF"


typedef char message[512];

typedef struct {
    bool is_active;
    int start_index;
    int end_index;
    bool indices[QUEUE_LEN];
    sem_t sem_auto[QUEUE_LEN];
}circular_queue;

typedef struct multicast_queue{
    message message_list[QUEUE_LEN];
    int last_index;
    circular_queue connections[CONN];
    int last_conn_index;
    sem_t remained_read[QUEUE_LEN];
    int num_conn;
    int queue_start;
    sem_t sem_write;
    sem_t sem_block;
    sem_t sem_fetch;
}multicast_queue;

void* auto_mode(void*);

bool first_read=false;
int conn_no;

int main(int argc, char const *argv[])
{
    //-------------------connect to server-----------------//
    int sock = 0;
    struct sockaddr_in serv_addr;
    char shmid_s[512] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }

    read( sock , shmid_s, 512); //-----read shared memory information over socket-----//
    int shmkey = atoi(shmid_s);
    int shmid;

    shmid = shmget(shmkey, SHM_SIZE, 0666 | IPC_CREAT);

    //---------------------attach to shared memory---------------------------//
    if(shmid == -1)
	{
		perror("shmget");
		exit(1);
	}
    multicast_queue *multicast_queue = shmat(shmid, NULL, 0);
	if(multicast_queue == (void*)(-1))
	{
		perror("shmat");
		exit(1);
	}
	conn_no=multicast_queue->last_conn_index;

	char *mode = NOAUTO;
    char command[7];
    memset(command,0,7);
    message messagebody;
    pthread_t id;

    int sem_val;
    sem_getvalue(&multicast_queue->sem_fetch, &sem_val);

    printf("Please enter your command\n");
    while(scanf("%s",command))
    {
        if(!strcmp(command,SEND))
        {
            sem_wait(&multicast_queue->sem_write);
            printf("Enter the message to send: ");
            scanf("%s", messagebody);
            send(sock, messagebody, strlen(messagebody), 0 );
        }
        else if(!strcmp(command,FETCH))
        {
            sem_getvalue(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index], &sem_val);

            if(sem_val == 0)
            {
                sem_post(&multicast_queue->sem_block);

                sem_wait(&multicast_queue->sem_fetch);

                sem_wait(&multicast_queue->sem_block);

                sem_getvalue(&multicast_queue->sem_block, &sem_val);
                if(sem_val>0)
                {
                    sem_post(&multicast_queue->sem_fetch);
                }
            }
            if(multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]==true)
            {
                sem_wait(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);
                sem_post(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);
            }
            message f_message;
            memcpy(f_message, multicast_queue->message_list[multicast_queue->connections[conn_no].end_index],512);

            sem_wait(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index]);

            multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]=true;

            sem_wait(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);

            multicast_queue->connections[conn_no].end_index = (multicast_queue->connections[conn_no].end_index+1)%QUEUE_LEN;

            printf("Message: %s\n", f_message);

        }
        else if(!strcmp(command,FETCHIF))
        {
            sem_getvalue(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index], &sem_val);
            if(sem_val != 0 && multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]!=true)
            {
                message f_message;
                memcpy(f_message, multicast_queue->message_list[multicast_queue->connections[conn_no].end_index],512);

                sem_wait(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index]);
                multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]=true;

                sem_wait(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);

                multicast_queue->connections[conn_no].end_index = (multicast_queue->connections[conn_no].end_index+1)%QUEUE_LEN;

                printf("Message: %s\n", f_message);
            }
            else{
                printf("There is no new message for this client\n");
            }
        }
        else if(!strcmp(command,AUTO))
        {
            mode = AUTO;
            pthread_create(&id, NULL, auto_mode, multicast_queue);
        }
        else if(!strcmp(command,NOAUTO))
        {
            mode = NOAUTO;
            pthread_cancel(id);
        }
        else if(!strcmp(command,QUIT))
        {
            send(sock, QUIT, strlen(QUIT), 0 );
            exit(1);
        }
        else{
            printf("There isn't such a command\n");
        }
        printf("Please enter your command\n");
    }


    return 0;
}
void* auto_mode(void* input_mqueue)
{
    struct multicast_queue* multicast_queue =  (struct multicast_queue*)input_mqueue;
    int sem_val;

    while(true)
    {
        if(multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]==true)
        {
            sem_wait(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);
            sem_post(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);
        }
        sem_getvalue(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index], &sem_val);

        if(sem_val == 0)
        {
            sem_post(&multicast_queue->sem_block);
            sem_getvalue(&multicast_queue->sem_block, &sem_val);

            sem_wait(&multicast_queue->sem_fetch);

            sem_wait(&multicast_queue->sem_block);

            sem_getvalue(&multicast_queue->sem_block, &sem_val);
            if(sem_val>0)
            {
                sem_post(&multicast_queue->sem_fetch);
            }
        }
        message f_message;
        memcpy(f_message, multicast_queue->message_list[multicast_queue->connections[conn_no].end_index],512);

        sem_wait(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index]);
        multicast_queue->connections[conn_no].indices[multicast_queue->connections[conn_no].end_index]=true;

        sem_wait(&multicast_queue->connections[conn_no].sem_auto[multicast_queue->connections[conn_no].end_index]);

        multicast_queue->connections[conn_no].end_index = (multicast_queue->connections[conn_no].end_index+1)%QUEUE_LEN;

        printf("Message: %s\n", f_message);
    }
}



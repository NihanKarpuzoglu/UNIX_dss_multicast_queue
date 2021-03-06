#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

#define CONN 100
#define QUEUE_LEN 1024
#define PORT 8090
#define SHM_SIZE (QUEUE_LEN*(512)+CONN*((4*2)+QUEUE_LEN+1+(32*QUEUE_LEN))+(4*4)+(3*32)+(QUEUE_LEN*32))
#define SHM_KEY 0x1234

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

typedef struct {
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

int main(int argc, char *argv[])
{

    //---- creating and attaching the shared memory---------------------
	int shmid;
	multicast_queue *multicast_queue;

	if(argc>2)
	{
		fprintf(stderr, "usage: shmdemo [data_to_write]\n");
		exit(1);
	}

	if((shmid = shmget(SHM_KEY, SHM_SIZE, 0666 | IPC_CREAT)) == -1)
	{
		perror("shmget");
		exit(1);
	}

	multicast_queue = shmat(shmid, NULL, 0); //---------creating queue data structure--------------
	if(multicast_queue == (void*)(-1))
	{
		perror("shmat");
		exit(1);
	}
    for(int j=0; j<CONN; j++)
    {
        multicast_queue->connections[j].end_index=0;
        multicast_queue->connections[j].is_active=false;
        multicast_queue->connections[j].start_index=0;
        for(int m=0; m<QUEUE_LEN; m++)
        {
            multicast_queue->connections[j].indices[m]=false;
            /*if(sem_init(&multicast_queue->connections[j].sem_auto[m], 1, 1) == -1)
            {
                perror("Could not create semaphore");
                exit(1);
            }*/
        }
    }
    for(int j=0; j<QUEUE_LEN; j++)
    {
        memset(multicast_queue->message_list[j],0,512);
    }

	/*if(argc == 2)
	{
		printf("writing to segment: \"%s\"\n", argv[1]);
		strncpy(multicast_queue->message_list[0], argv[1], SHM_SIZE);
	}
	else
	{
		printf("segment contains: \"%s\"\n", multicast_queue->message_list[0]);
	}*/

    if(sem_init(&multicast_queue->sem_write, 1, 1) == -1)
    {
        perror("Could not create semaphore");
        exit(1);
    }
    for(int rem_read=0;rem_read<QUEUE_LEN;rem_read++)
    {
        if(sem_init(&multicast_queue->remained_read[rem_read], 1, 0) == -1)
        {
            perror("Could not create semaphore");
            exit(1);
        }
    }
    if(sem_init(&multicast_queue->sem_block, 1, 0) == -1)
    {
        perror("Could not create semaphore");
        exit(1);
    }

    if(sem_init(&multicast_queue->sem_fetch, 1, 0) == -1)
    {
        perror("Could not create semaphore");
        exit(1);
    }


	//---------------------binding and listening to the unix stream socket-------------------------------------------------------

    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

	// Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( PORT );

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    multicast_queue->last_conn_index=0;
    multicast_queue->last_index=0;
    multicast_queue->num_conn=0;
    multicast_queue->queue_start=0;

    bool is_start=true;
    int read_r=0;
    pid_t fork_val;

    while((new_socket=accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)))
    {
        fork_val=fork();
        if(fork_val==-1)
        {
            close(new_socket);
            printf("Could not fork");
        }
        if(fork_val) //handle parent process
        {
            close(new_socket);
        }
        else{ //handle agent process
            close(server_fd);

            if(multicast_queue->num_conn < CONN)
            {
                multicast_queue->num_conn++;

                while(multicast_queue->connections[multicast_queue->last_conn_index].is_active == true)
                {
                    multicast_queue->last_conn_index = (multicast_queue->last_conn_index+1)%CONN;
                }

                multicast_queue->connections[multicast_queue->last_conn_index].is_active=true;
                multicast_queue->connections[multicast_queue->last_conn_index].start_index=multicast_queue->last_index;
                multicast_queue->connections[multicast_queue->last_conn_index].end_index=multicast_queue->queue_start;

                for(int k=0; k<QUEUE_LEN; k++)
                {
                    if(sem_init(&multicast_queue->connections[multicast_queue->last_conn_index].sem_auto[k], 1, 1) == -1)
                    {
                        perror("Could not create semaphore");
                        exit(1);
                    }
                }

                int conn_no=multicast_queue->last_conn_index;

                int i=multicast_queue->queue_start;

                char shmid_addr[512];
                sprintf(shmid_addr, "%d", SHM_KEY);
                send(new_socket, shmid_addr, strlen(shmid_addr), 0 );  //send shared memory information over socket

                if(strcmp(multicast_queue->message_list[0],""))
                {
                    int z;

                    sem_post(&multicast_queue->remained_read[i]);
                    multicast_queue->connections[conn_no].indices[i]=false;

                    sem_getvalue(&multicast_queue->connections[conn_no].sem_auto[i], &z);
                    if(z == 0)
                    {
                        sem_post(&multicast_queue->connections[conn_no].sem_auto[i]);
                    }
                    i = (i+1)%QUEUE_LEN;
                }
                while(i != multicast_queue->queue_start)
                {
                    if(strcmp(multicast_queue->message_list[i],""))
                    {
                        sem_post(&multicast_queue->remained_read[i]);
                        multicast_queue->connections[conn_no].indices[i]=false;

                        int z;
                        sem_getvalue(&multicast_queue->connections[conn_no].sem_auto[i], &z);

                        if(z == 0)
                        {
                            sem_post(&multicast_queue->connections[conn_no].sem_auto[i]);
                        }
                    }
                    i = (i+1)%QUEUE_LEN;
                }
                message messagebody;
                while(true)
                {
                    read( new_socket , messagebody, 512);
                    if(!strcmp(messagebody,QUIT))
                    {
                        multicast_queue->num_conn--;
                        while(multicast_queue->connections[conn_no].end_index != multicast_queue->last_index)
                        {
                            sem_wait(&multicast_queue->remained_read[multicast_queue->connections[conn_no].end_index]);

                            multicast_queue->connections[conn_no].end_index = (multicast_queue->connections[conn_no].end_index+1)%QUEUE_LEN;
                        }
                        multicast_queue->connections[conn_no].is_active=false;
                        /*for(int k=0; k<QUEUE_LEN; k++)
                        {
                            sem_post(&multicast_queue->connections[i].sem_auto[multicast_queue->last_index]);
                            sem_destroy(&multicast_queue->connections[conn_no].sem_auto[k]);
                        }*/
                        printf("Exiting this agent process\n");
                        exit(1);
                    }
                    else
                    {
                        int sem_value;
                        sem_getvalue(&multicast_queue->remained_read[multicast_queue->last_index],&sem_value);

                        if(sem_value == 0)//if queue is not full
                        {
                            if(strcmp(multicast_queue->message_list[multicast_queue->last_index],""))
                            {
                                multicast_queue->queue_start = (multicast_queue->queue_start+1)%QUEUE_LEN;
                            }

                            for(int i=0;i<CONN;i++)
                            {
                                multicast_queue->connections[i].indices[multicast_queue->last_index]=false;

                                int z;

                                sem_getvalue(&multicast_queue->connections[i].sem_auto[multicast_queue->last_index], &z);
                                if(z == 0)
                                {
                                    sem_post(&multicast_queue->connections[i].sem_auto[multicast_queue->last_index]);
                                }
                            }
                            printf("Recording message: %s\n", messagebody);
                            memcpy(multicast_queue->message_list[multicast_queue->last_index],messagebody,512);

                            for(int l=0;l<multicast_queue->num_conn;l++)
                            {
                                sem_post(&multicast_queue->remained_read[multicast_queue->last_index]);
                            }
                            int sem_val;
                            sem_getvalue(&multicast_queue->sem_block, &sem_val);
                            if(sem_val>0)
                            {
                                sem_post(&multicast_queue->sem_fetch);
                            }
                            multicast_queue->last_index = (multicast_queue->last_index+1)%QUEUE_LEN;
                        }
                        else{
                            printf("Message queue is full. Cannot write a message\n");
                        }
                        memset(messagebody,0,strlen(messagebody));
                        sem_post(&multicast_queue->sem_write);
                    }
                }
            }
            else{
                printf("There is no space for a new connection\n");
            }
        }
    }
    int dt;
	if((dt = shmdt(multicast_queue)) == -1)
	{
		perror("shmdt");
		exit(1);
	}

	if(shmctl(shmid, IPC_RMID, NULL) < 0)////
	{
		perror("shmctl");
		exit(1);
	}

	printf("Everything is ok6\n");

	return 0;
}



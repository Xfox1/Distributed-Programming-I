/*
	|-------------------------------------------------------|
	| Author: Antonio Mignano				|
	| Purpose: TCP Server Template (Echo)			|
	| 							|
	| Features: 						|
	| 	->	Preforking (pooling)			|
	| 	->	Infinite accept loop			|
	|	->	Zombie avoidance			|
	| Configure defines:					|		
	|	->	NSERVERS = number of thread in pool	|
	|	->	BKLOG = Length of pending request queue	|
	|	->	BUFLEN = Buffers length			|
	|-------------------------------------------------------|		
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <signal.h>

#include "../sockwrap.h"
#include "../errlib.h"

#define NSERVERS 3
#define BKLOG 3
#define BUFLEN 1024
#define TIMEOUTSELECT 10

//Global variables
char *prog_name; //Needed by errlib
int *childs; //Array of threads' pids

//Prototypes
void newServer(int mySocket, int pid);


//Handler for SICHLD
void handlerCHLD(int sig){
	pid_t pid;
	pid = wait(NULL);
	printf("[F] Pid %d exited.\n", pid);
}

//Handler for SIGINT
void handlerFTR(int sig){
	int i;
	printf("[F] Killing childs :'(\n");
	for(i = 0; i < NSERVERS; i++){
		kill(childs[i], SIGKILL);
	}
}




int main(int argc, char *argv[]){
	prog_name = argv[0]; //Needed by errlib

	if(argc != 2){
		printf("Usage: %s <port_no>\n", argv[0]);
		return -1;
	}

	int port = atoi(argv[1]);

	struct sockaddr_in saddr; //Server structure
	int mySocket; //Pasive socket descriptor


	mySocket = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //Creating passive socket (TCP)
	if(mySocket < 0){
		printf("Error creating socket\n");
		return -1;
	}

	bzero(&saddr, sizeof(saddr)); //Cleaning saddr structure
	saddr.sin_family      = AF_INET;
	saddr.sin_port        = htons(port); //htons = host to network short
	saddr.sin_addr.s_addr = INADDR_ANY; //All local IP addresses
	Bind(mySocket, (struct sockaddr *) &saddr, sizeof(saddr)); //Binding socket

	Listen(mySocket, BKLOG); //BKLOG = Length of pending request queue

	//Allocating vector for threads' pids
	childs = (int*) malloc(NSERVERS * sizeof(int));
	
	int childPid;
	int i;

	for(i = 0; i < NSERVERS; i++){
		childPid = fork();
		
		if(childPid < 0){
			printf("Error in fork()\n");
			return -1;
		}else if(childPid == 0){ //Child
			printf("[TH-%d] Starting...\n", i);
			newServer(mySocket, i);
			return -1; //Will never happen because of while(1) in newServer(...)
		}else{ //Father
			childs[i] = childPid;
		}
	}

	//Handling signals
	signal(SIGCHLD, handlerCHLD);
	signal(SIGINT, handlerFTR);

	pause(); //Waiting for signals (actually SIGINT)
	return 0;	
} //End of main()

void newServer(int mySocket, int pid){

	//Structure for incoming connections
	struct sockaddr_in from;
 	socklen_t addrlen;

	int rSocket; //Active socket
	uint32_t nBytesReceived, nBytesSent;

	//If file size/last mod needed
	FILE *fp; //File to send
	struct stat fileStat;

	char buffer[BUFLEN];
	uint16_t fileNameSize;
	char fileName[BUFLEN];
	char receivedByte, byteSent;
	
	int failureFlag = 0;
	uint8_t command;
	
	uint32_t i;

	fd_set cset;
	struct timeval tval;


	while(1){ //Accepting loop
		printf("[TH-%d] Waiting fo connection...\n", pid);

		addrlen = sizeof(struct sockaddr_in);		
		rSocket = Accept(mySocket, (struct sockaddr*) &from, &addrlen);

		printf("[TH-%d] Connection accepted [%s:%u] -> Waiting for data...\n", pid, inet_ntoa(from.sin_addr), ntohs(from.sin_port));

		//TIMEOUT HERE
		FD_ZERO(&cset);
		FD_SET(rSocket, &cset);
		tval.tv_sec = TIMEOUTSELECT;
		tval.tv_usec = 0;

		int n = select(rSocket+1, &cset, NULL, NULL, &tval);
		if(n <= 0){
			printf("[TH-%d] No data received. Timeout occurred!\n", pid);
			close(rSocket);
			continue;
		}

		//---------- DO STUFF HERE ---------
		while(1){ //Serving one client loop
			memset(buffer, 0, BUFLEN);

			nBytesReceived = readn(rSocket, &command, 1);
			if(nBytesReceived <= 0){
				printf("[TH-%d] Error in readn(COMMAND)\n", pid);
				close(rSocket);
				break;			
			}

			printf("[TH-%d] Command received: %d\n", pid, command);

			if(command == 0){ //GET COMMAND
				//FilenameSize
				nBytesReceived = readn(rSocket, &fileNameSize, 2);
				if(nBytesReceived <= 0){
					printf("[TH-%d] Error in readn(fileNameSize)\n", pid);
					close(rSocket);
					break;;		
				}

				fileNameSize = ntohs(fileNameSize);
	
				printf("[TH-%d] Received filenamesize: %u\n", pid,fileNameSize);
				//Receiving fileName

				for(i = 0; i < fileNameSize; i++){
					nBytesReceived = readn(rSocket, &fileName[i], 1);
					if(nBytesReceived <= 0){
						printf("[TH-%d] Error in readn(fileName)\n", pid);
						close(rSocket);
						failureFlag = 1;				
					}
				}
				fileName[i] = '\0';
				printf("[TH-%d] File name requested %s\n", pid, fileName);
				if(failureFlag){
					break;
				}


				//Sending file
				if((fp = fopen(fileName, "r")) == NULL){ //File not found
					printf("[TH-%d] Error: file %s not found\n", pid, fileName);

					byteSent = 3; //ERR
					nBytesSent = writen (rSocket, &byteSent, 1);
					if(nBytesSent <= 0){
						printf("[TH-%d] Error in sending ERR file not found)\n", pid);
						close(rSocket);
						break;
					}
				}else{ //File opened
					if(stat(fileName, &fileStat) < 0){
						printf("[TH-%d] Error in stat)\n", pid);
						close(rSocket);
						break;
					} 

					printf("File size: %u\tLast edit: %u\n", fileStat.st_size, fileStat.st_mtime);
					byteSent = 1; //OK
					nBytesSent = writen (rSocket, &byteSent, 1);
					if(nBytesSent <= 0){
						printf("[TH-%d] Error in sending OK)\n", pid);
						close(rSocket);
						break;
					}
					
					uint32_t fsize = htonl(fileStat.st_size);
					uint32_t fmod = htonl(fileStat.st_mtime);


					failureFlag = 0;
					if(writen (rSocket, &fsize, 4) <= 0){
						failureFlag = 1;
					}
					if(writen (rSocket, &fmod, 4) <= 0){
						failureFlag = 1;
					}
					if(failureFlag){
						printf("[TH-%d] Error sending filesize/lastmod\n", pid);
						close(rSocket);
						break;
					}

					char myChar;
					int i = 1;
					failureFlag = 0;

					uint32_t alreadyfileRead = 0;
					uint32_t fileRead = 0;
					
					while(alreadyfileRead <= ntohl(fsize)){
						fileRead = fread(buffer, 1, BUFLEN, fp);
						if(writen(rSocket, buffer, fileRead) <= 0){
							printf("[TH-%d] Error sending part of file. alreadyfileRead: %u\tout of: %u\n", pid, alreadyfileRead, ntohl(fsize));
							failureFlag = 1;
							break;
						}
						alreadyfileRead += fileRead;
					}					

					if(failureFlag && (alreadyfileRead != ntohl(fsize))){
						printf("[TH-%d] About to break!\n", pid);
						close(rSocket);
						break;
					}

					printf("[TH-%d] File transfer complete! Waiting for something else...\n", pid);

					//TIMEOUT HERE
					FD_ZERO(&cset);
					FD_SET(rSocket, &cset);
					tval.tv_sec = TIMEOUTSELECT;
					tval.tv_usec = 0;

					int n = select(rSocket+1, &cset, NULL, NULL, &tval);
					if(n <= 0){
						printf("[TH-%d] No data received. Timeout occurred!\n", pid);
						close(rSocket);
						break;
					}

					close(rSocket);
					break;
				}
			}else{ //NOT GET COMMAND
				if(command == 2){
					printf("[TH-%d] Quit command received\n", pid);
					close(rSocket);
					break;
				}else{
					printf("[TH-%d] Unknown command received\n", pid);
					close(rSocket);
					break;
				}
			}

		} //End of serving one client loop

		//---------- DO STUFF HERE ---------



	} //End of accepting loop
} //End of newServer()

/*
	|-------------------------------------------------------|
	| Author: Antonio Mignano				|
	| Purpose: TCP Client Template (Echo)			|
	| 							|
	| Features: 						|
	| 	->	Timeout	( = select )			|
	| Configure defines:					|		
	|	->	TIMEOUT = seconds			|
	|	->	BUFLEN = Buffers length			|
	|-------------------------------------------------------|		
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <signal.h>

#include "../sockwrap.h"
#include "../errlib.h"

#define TIMEOUT 5
#define BUFLEN 1024

//Global variables
char *prog_name; //Needed by errlib

//Prototypes
void newClient(int mySocket, char *filename);

//Handler for SIGINT
void handlerFTR(int sig){
	printf("\n---------- Client stopped! ----------\n");
	exit(0); //Terminating client
}

int main(int argc, char *argv[]){

	prog_name = argv[0]; //Needed by errlib

	if(argc != 4){
		printf("Usage: %s <server_addr> <port_no> <filename>\n", argv[0]);
		return -1;
	}

	char *address = argv[1];
	int port = atoi(argv[2]);
	char *filename = argv[3];

	//Structure for connection
	struct sockaddr_in saddr;
	int mySocket;
	int connection;

	mySocket = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	bzero(&saddr, sizeof(saddr)); //Cleaning saddr structure
	saddr.sin_family      = AF_INET;
	saddr.sin_port        = htons(port); //htons = host to network short
	saddr.sin_addr.s_addr = inet_addr(address); //All local IP addresses

	connection = connect(mySocket, (struct sockaddr *) &saddr, sizeof(saddr));

	if(connection < 0){
		printf("connect() failed\n");
		return -1;
	}else{
		printf("Correctly connected to the %s:%d\n", address, port);
	}

	signal(SIGINT, handlerFTR);

	newClient(mySocket, filename);

	return 0;
} //End of main()



void newClient(int mySocket, char *filename){

	char buffer[BUFLEN];
	uint32_t nBytesReceived, nBytesSent;

	//fd_set is used in Select (cset is like an array of sockets)
	fd_set cset; 
	struct timeval tval; //Used in Select for timeout
	
	uint8_t command;
	uint32_t i;
	int failureFlag = 0;

	uint32_t filesize;
	uint32_t lastMod;

	uint8_t response;

	FILE *fp;

	command = 0;
	//Sending GET
	nBytesSent = writen (mySocket, &command, 1);
	if(nBytesSent <= 0){
		printf("Error sending GET\n");
		close(mySocket);
		return -1;
	}

	printf("Sendinf filenamesize: %u\n",strlen(filename));
	uint16_t filenameSize = htons(strlen(filename));
	//Sending two bytes for file name size
	nBytesSent = writen (mySocket, &filenameSize, 2);
	if(nBytesSent <= 0){
		printf("Error sending file name size\n");
		close(mySocket);
		return -1;
	}

	
	printf("Sending fileName: ");
	failureFlag = 0;
	//sending filename
	for(i = 0; i < strlen(filename); i++){
		nBytesSent = writen (mySocket, &filename[i], 1);

		printf("%c", filename[i]);
		if(nBytesSent <= 0){
			printf("Error sending file name size\n");
			close(mySocket);
			failureFlag = 1;
		}
	}
	printf("\n");

	if(failureFlag){
		return -1;
	}


	printf("Waitinf for answer...\n");
	//Receiving answser 1 byte
	nBytesReceived = readn(mySocket, &response, 1);
	if(nBytesReceived <= 0){
		printf("Error receiving answer from server\n");
		close(mySocket);
		return -1;			
	}

	printf("Response: %u\n", response);
	if(response == 1){ //OK
		//Receiving file size 4 byte
		nBytesReceived = readn(mySocket, &filesize, 4);
		if(nBytesReceived <= 0){
			printf("Error receiving fileSize from server\n");
			close(mySocket);
			return -1;			
		}

		//Receiving last Mod size 4 byte	
		nBytesReceived = readn(mySocket, &lastMod, 4);
		if(nBytesReceived <= 0){
			printf("mySocket receiving lastMod from server\n");
			close(mySocket);
			return -1;			
		}	

		printf("Received fileSize: %u\tlastmod: %u\n", ntohl(filesize), ntohl(lastMod));

		//Receiving file

		filesize = ntohl(filesize);
		uint32_t totalSizeReceived = 0;

		if((fp = fopen(filename, "w")) == NULL){
			printf("Error writing file!\n");
			close(mySocket);
		}

		while(totalSizeReceived < filesize){
			nBytesReceived = readn(mySocket, buffer, BUFLEN);
			
			if(nBytesReceived <= 0){
				printf("Error receiving part of file from server\n");
				close(mySocket);
				return -1;			
			}	
			totalSizeReceived += nBytesReceived;
			printf("Received: %u\tout of %u\n", totalSizeReceived, filesize);
			fwrite(buffer, 1, nBytesReceived, fp);
		}
		fclose(fp);
		printf("Transfer complete!\n");
		
		close(mySocket);

	}else{ //Response NOT OK
		if(response == 3){
			printf("File not found\n");
			close(mySocket);
			return -1;
		}else{
			printf("Unknown response code\n");
			close(mySocket);
			return -1;
		}
	}		

} //End of newClient()
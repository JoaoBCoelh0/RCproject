#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define TAM 1024    // Tamanho do buffer
#define MULTICAST_PORT 5000


int fd, client;
struct sockaddr_in multicast;
int client_addr_size, addrlen;
struct ip_mreq mreq;
int sock, cnt;

void erro(char *s) {
    perror(s);
    exit(1);
}

void task(char ip_address[TAM]);


int main(int argc, char *argv[]) {
	
	if (argc != 3) {
		printf("operations_terminal <endereco_servidor> <porto_bolsa>\n");
		exit(-1);
	}
	
	char endServer[TAM];
	struct sockaddr_in addr;
	struct hostent *hostPtr;
	strcpy(endServer, argv[1]);
	if ((hostPtr = gethostbyname(endServer)) == 0)
		erro("Endereço nao recebido");
	
	int fd;
	
	bzero((void *) &addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ((struct in_addr *)(hostPtr->h_addr))->s_addr;
	addr.sin_port = htons((short) atoi(argv[2]));
	
	if ((fd = socket(AF_INET,SOCK_STREAM,0)) == -1)
		erro("socket");
	if (connect(fd,(struct sockaddr *)&addr,sizeof (addr)) < 0)
		erro("Connect");
	
	char username[TAM];
	char password[TAM];
	char buffer[TAM];
	char text[TAM];
	int count = 0;
	pid_t pid;
	
		
	
	printf("Username: ");
	fgets(username, TAM, stdin);
	username[strlen(username)-1] = '\0';
	
	write(fd, username, strlen(username)+1);
	fflush(stdout);
	
	printf("Password: ");
	fgets(password, TAM, stdin);
	password[strlen(password)-1] = '\0';
	
	write(fd, password, strlen(password)+1);
	fflush(stdout);
	
	read(fd, buffer, TAM);
	puts(buffer);
	fflush(stdout);
	
	bzero(buffer, strlen(buffer));
	

	while(1) {
		fgets(buffer, TAM, stdin);
		write(fd, buffer, strlen(buffer)+1);
		fflush(stdout);
		
		char *token = strtok(buffer, " \n");
        char buff[2][TAM];
        int controlador = 0;
        while (token != NULL) {
            strcpy(buff[controlador],token);
            controlador++;
            token = strtok(NULL, " \n");
        }
        
        if (strcmp(buff[0], "0") == 0){
			if (count == 0){
				count = 1;
				
				bzero(buffer, TAM);
				int nread = read(fd, buffer, TAM);
				buffer[nread] = '\0';
				
				if ((pid = fork()) == 0) {
					task(buffer);
					exit(0);
				}
			}else if (count == 1){
				kill(pid, SIGKILL);
				count = 0;
			}
        	
        }else if (strcmp(buff[0],"BUY") == 0){
			int nread = read(fd, text, TAM);
			text[nread] = '\0';
			puts(text);
			bzero(text, TAM);
			
		}else if (strcmp(buff[0], "3") == 0) {
			kill(pid, SIGKILL);
			close(fd);
			return 0;
		}else if (strcmp(buff[0], "2") == 0) {
			int nread = read(fd, text, TAM);
			text[nread] = '\0';
			puts(text);
			bzero(text, TAM);
		} 
		bzero(buffer, strlen(buffer));
	}
	
	return 0;	  
}

void task(char ip_address[TAM]) {

	char text[TAM]; 
	
	mreq.imr_multiaddr.s_addr = inet_addr(ip_address); 
	mreq.imr_interface.s_addr = htonl(INADDR_ANY); 
	
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	int multicastTTL = 255;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *) &multicastTTL, sizeof(multicastTTL)) < 0) {
		perror("setsockopt mreq");
		exit(1);
	}
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    	erro("setsockopt mreq");
    }

	bzero((char *)&multicast, sizeof(multicast));
	multicast.sin_family = AF_INET;
	multicast.sin_addr.s_addr = htonl(INADDR_ANY);
	multicast.sin_port = htons(MULTICAST_PORT);
	
	if (bind(sock, (struct sockaddr *) &multicast, sizeof(multicast)) < 0) { 
		perror("bind");
		exit(1);
	} 

	while(1) {
		cnt = recvfrom(sock, text, sizeof(text), 0, (struct sockaddr *) &multicast, (socklen_t *)sizeof(multicast));
		puts(text);
		sleep(2);
	}
	
	if(setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq))< 0){
		erro("setsockpt mreq");
	}
	
}



	  

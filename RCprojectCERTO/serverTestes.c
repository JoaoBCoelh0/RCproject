#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT_TCP 6000
#define PORT_UDP 7000
#define MULTICAST_PORT 5000
#define MULTICAST_GROUP "239.0.0.1"

#define BUFLEN 1024  
#define PORT 9876    

#define TAM 1024
#define NACOES 3    //Numero max de acoes
#define NMARKETS 2  //Numero max de mercados
#define NUSERS 10   //Numero max de users
#define NPARAMETERS 5 //Numero max de parametros possiveis



struct admin {
    char nome[TAM]; //Nome utilizador
    char password[TAM]; //Password utilizador
};

struct stock {
    char nome[TAM]; //Nome stock
    double valor;   //Valor do stock
};

struct market {
    char nome[TAM]; //Nome do mercado
    struct stock lista[NACOES];
};

struct carteira{
    char nomeAcao[TAM];
    int numAcoes;
};

struct user {
    char nome[TAM]; //Nome utilizador
    char password[TAM]; //Password utilizador
    double saldo;   //Saldo utilizador
    char market1[TAM];  //Mercado a que tem acesso
    char market2[TAM];  //Mercado a que tem acesso
    char marketsSub[NMARKETS][TAM];
    struct carteira listaCarteira[6];
};

struct memoriaPartilhada{
    struct admin administrador;
    struct user listaUsers[NUSERS];
    struct market listaMarkets[NMARKETS];
    int refreshTime;
};


struct memoriaPartilhada *shmem;
int shmid;

//UDP
struct sockaddr_in si_minha, si_outra;
int s, recv_len;
socklen_t slen = sizeof(si_outra);
char buf[BUFLEN];

//TCP
int fd, client;
struct sockaddr_in addr, client_addr;
int client_addr_size;

//MULTICAST
struct sockaddr_in multicast;
int addrlen,sock, cnt;

//threads refresh
pthread_t my_t;
int id;

void erro(char *s) {
    perror(s);
    exit(1);
}

int parseFile();
int inicializacao();

int addUser(char name[TAM], char pass[TAM], double saldo, char market1[TAM], char market2[TAM]);

int deleteUser(char nome[TAM]);

char *listUsers();


int gerirCliente();
double compra(int indexUser,char nome[TAM], double preco,int nAcoes);

void feed(int i);
void sub_unsub(int i, char splitedBuf[3][TAM]);
double venda(char acao[TAM], int user , int numAcoes ,double price);


void *refresh(void *time) {
    int *t = (int *) time;
    while (1) {
        sleep(*t);
        int i, j;
        for (i = 0; i < NMARKETS; i++) {
            for (j = 0; j < NACOES; j++) {
                int r = rand() % 10;
                if (r < 5) {
                    shmem->listaMarkets[i].lista[j].valor += 0.01;

                } else {
                    if (shmem->listaMarkets[i].lista[j].valor - 0.01 >= 0.01) {
                        shmem -> listaMarkets[i].lista[j].valor -= 0.01;
                    }
                }
            }
        }
    }
}

int main(void) {

    int id = 2;
    

    pthread_t my_t;

    pthread_create(&my_t, NULL, refresh, &id);

   

    //Memoria Partilhada----
    shmid = shmget(8888, sizeof(struct memoriaPartilhada), IPC_CREAT | 0766);
    if (shmid == -1){
        perror("Failed to create shm!");
        return(1);
    }

    shmem = shmat(shmid, NULL, 0);
    shmem->refreshTime = 2;
    //----
	
	inicializacao();
    parseFile();


    //TCP------------
    bzero((void *) &addr, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT_TCP);

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        erro("na funcao socket");
    if (bind(fd,(struct sockaddr*)&addr,sizeof(addr)) < 0)
        erro("na funcao bind");
    if(listen(fd, 5) < 0)
        erro("na funcao listen");
    client_addr_size = sizeof(client_addr);
    //---------------

    //Multicast-----
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("erro multicast");
        exit(1);
    }

    int multicastTTL = 255;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &multicastTTL, sizeof(multicastTTL)) < 0){
        perror("erro multicast opt");
        exit(1);
    }

    bzero((char *)&addr, sizeof(addr));
    multicast.sin_family = AF_INET;
    multicast.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast.sin_port = htons(MULTICAST_PORT);
    multicast.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    addrlen = sizeof(multicast);
    //----------------


    if (fork() == 0){
        while(1){
            while(waitpid(-1,NULL,WNOHANG)>0);

            client = accept(fd,(struct sockaddr *)&client_addr,(socklen_t *)&client_addr_size);

            if (client < 0){
                erro("Erro a aceitar cliente ! ");
            }
            else{
                if(fork() == 0){
                    gerirCliente();
                    close(fd);
                    close(client);
                    exit(0);

                }
            }
        }
    }
	
	//UDP----------

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        erro("Erro na criação do socket");
    }

    si_minha.sin_family = AF_INET;
    si_minha.sin_port = htons(PORT_UDP);
    si_minha.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *) &si_minha, sizeof(si_minha)) == -1) {
        erro("Erro no bind");
    }
    //-----------------

    bool validacaoAdmin = true;

    while(validacaoAdmin){
        char dadosLogin[TAM];
        if ((recv_len = recvfrom(s, dadosLogin, BUFLEN, 0, (struct sockaddr *) &si_outra, (socklen_t *) &slen)) == -1) {
            erro("Erro no recvfrom");
        }
        dadosLogin[recv_len] = '\0';

        char *token = strtok(dadosLogin, " ");
        char infoLogin[2][TAM];
        int aux = 0;

        while (token != NULL) {
            strcpy(infoLogin[aux], token);
            aux++;
            token = strtok(NULL, " ");
        }



        if((strcmp(infoLogin[0],shmem->administrador.nome) == 0) && (strcmp(infoLogin[1],shmem->administrador.password) == 0)){
            char menAux[TAM];
            strcpy(menAux,"Bem vindo!\n");
            sendto(s, menAux, strlen(menAux) + 1, 0, (struct sockaddr *) &si_outra, sizeof(si_outra));
            validacaoAdmin = false;
        }else{
            char menAux[TAM];
            strcpy(menAux,"Nome ou password errados, separe o nome da password com um espaco!!\n");
            sendto(s, menAux, strlen(menAux) + 1, 0, (struct sockaddr *) &si_outra, sizeof(si_outra));
        }
		
    }

    while (1) {

        // Espera recepção de mensagem (a chamada é bloqueante)
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_outra, (socklen_t *) &slen)) == -1) {
            erro("Erro no recvfrom");
        }
        // Para ignorar o restante conteúdo (anterior do buffer)

        
        buf[recv_len] = '\0';
        
        char bufInstructions[NPARAMETERS][TAM];
        int aux = 0;

        char *token = strtok(buf, " ");

        while (token != NULL) {
            strcpy(bufInstructions[aux], token);
            aux++;
            token = strtok(NULL, " ");
        }

        if (strcmp(bufInstructions[0], "ADD_USER") == 0) {
            char *token = strtok(bufInstructions[3], "/");
            int aux = 0;

            char market1[TAM];
            char market2[TAM];
            while (token != NULL) {
                if (aux == 0) {
                    strcpy(market1, token);
                    aux = 1;
                } else {
                    strcpy(market2, token);
                }
                token = strtok(NULL, "/");
            }
            bufInstructions[4][strlen(bufInstructions[4]) - 1] = '\0';   //retirar o \n

            addUser(bufInstructions[1], bufInstructions[2], atoi(bufInstructions[4]), market1, market2);

        } else if (strcmp(bufInstructions[0], "DEL") == 0) {
            bufInstructions[1][strlen(bufInstructions[1]) - 1] = '\0';

            deleteUser(bufInstructions[1]);
        } else if (strcmp(buf, "LIST\n") == 0) {
            char *textoEnviar = listUsers();
            sendto(s, textoEnviar, strlen(textoEnviar) + 1, 0, (struct sockaddr *) &si_outra, sizeof(si_outra));

        } else if (strcmp(bufInstructions[0], "REFRESH") == 0) {
            pthread_cancel(my_t);
            id = atoi(bufInstructions[1]);
            pthread_create(&my_t, NULL, refresh, &id);
        } else if (strcmp(buf, "QUIT\n") == 0) {
            return 0;
        } else if (strcmp(buf, "QUIT_SERVER\n") == 0) {
            pthread_cancel(my_t);
            return 0;
        }
    }

}


int gerirCliente(){
    char buffer[BUFLEN];
	char userName[TAM];
	char userPass[TAM];
    int i = 0;
    bool auxLogin = false;
	int nread;
	
    nread = read(client,userName,BUFLEN);
	userName[nread]='\0';
	
    nread = read(client,userPass,BUFLEN);
	userPass[nread]='\0';

    for(i = 0; i < NUSERS;i++){
        if((strcmp(userName, shmem -> listaUsers[i].nome) == 0) && (strcmp(userPass,shmem -> listaUsers[i].password) == 0)){
			strcpy(buffer, "Login bem sucedido, bem vindo!!\nMercados disponiveis: \n");
            strcat(buffer, shmem -> listaUsers[i].market1);
            strcat(buffer, "\n");
            strcat(buffer, shmem -> listaUsers[i].market2);
            strcat(buffer, "\n");

            auxLogin = true;
            break;
        }
    }
    if (auxLogin == false){
        printf("Erro no login !!\n");
        return 0;
    }

    
    strcat(buffer, "\n-----------------------MENU------------------------\n");
	strcat(buffer, "Ligar/Desligar feed de atualizacoes -> 0\n");
    strcat(buffer, "Subescrever/Cancel subscricao de cotacoes -> 1 [nome do/s mercado/s] \n");
    strcat(buffer, "Comprar acoes -> 4 acao quantidade preco\n");
    strcat(buffer, "Vender acoes -> 5 acao quantidade preco\n");
    strcat(buffer, "Mostrar carteira e saldo -> 2\n");
    strcat(buffer, "QUIT -> 3\n");
    //------------

    write(client, buffer, strlen(buffer) + 1);
    bzero(buffer, BUFLEN);
    pid_t feedpid;
	char txt[BUFLEN];
	int count = 0;
	
	
    while(1) {
        nread = read(client, buf, BUFLEN);
        buf[nread] = '\0';
        char *token = strtok(buf, " \n");
        char sptxt[4][TAM];
		int h=0;
		for (h=0; h<4; h++ ){
			bzero(sptxt[h], TAM);
		}
        
        int aux = 0;
        while (token != NULL) {
            strcpy(sptxt[aux],token);
            aux++;
            token = strtok(NULL, " \n");
        }
		//feed
		
        if(strcmp(sptxt[0],"0") == 0){
			if(count == 0){
				write(client, MULTICAST_GROUP, TAM);
				
				if ((feedpid = fork())== 0) {
					feed(i);
					exit(0);
				}
				
				count = 1;
			}else if(count == 1){
				kill(feedpid, SIGKILL);
				count = 0;
			}
			
        }else if(strcmp(sptxt[0],"1") == 0){
            sub_unsub(i, sptxt);
			
        }else if (strcmp(sptxt[0],"4") == 0){
            double val;
            if ((val = compra(i,sptxt[1],atof(sptxt[3]), atoi(sptxt[2]))) == 0) {
                strcpy(txt, "Nao comprou");
                write(client, txt, BUFLEN);
            }else {
                sprintf(txt, "Compra efetuada($): %f\n", val);
                write(client, txt, BUFLEN);
            }
            bzero(txt, BUFLEN);
        }else if (strcmp(sptxt[0],"5") == 0){
            double val;
            if (val = venda(sptxt[1],i,atoi(sptxt[2]),atof(sptxt[3])) == 0) {
                strcpy(txt, "Nao comprou");
                write(client, txt, BUFLEN);
            }else {
                sprintf(txt, "Venda efetuada($): %f\n", val);
                write(client, txt, BUFLEN);
            }
            bzero(txt, BUFLEN);
        }else if (strcmp(sptxt[0],"2") == 0) {
            int l;
            char auxtxt[TAM*2];
            sprintf(txt, "Saldo: %f\n", shmem->listaUsers[i].saldo);

            for (l = 0; l < 6; l++){
                if (shmem->listaUsers[i].listaCarteira[l].nomeAcao[0] != '\0') {
                    sprintf(auxtxt, "%s -> %d\n", shmem->listaUsers[i].listaCarteira[l].nomeAcao, shmem->listaUsers[i].listaCarteira[l].numAcoes);
                    strcat(txt, auxtxt);
                    bzero(auxtxt, BUFLEN);
                }
            }
            write(client, txt, BUFLEN);
            bzero(txt, BUFLEN);
        }else if (strcmp(sptxt[0],"3") == 0) {
            return 0;
        }
    }
    return 0;
}

int parseFile() {
    FILE *config;
    config = fopen("configs.txt", "r");
    if (!config) {
        return 1;
    }

    int controlador = 0;
    char buffer[TAM];
    char *token;
    int i =0;
    int c=-1;
    int d=0;
    int nusers = 0;
    char auxt[TAM];
    auxt[0]='\0';
    while (fgets(buffer, sizeof(buffer), config)) {

        int aux = 0;


        if (controlador == 0) {

            token = strtok(buffer, "/");
            while (token != NULL) {
                if (aux == 0) {
                    strcpy(shmem -> administrador.nome, token);
                    aux = 1;
                } else {
                    strcpy(shmem -> administrador.password, token);
                    break;
                }
                token = strtok(NULL, "/");
            }
            controlador = 1;

        } else if (controlador == 1) {

            nusers = atoi(buffer);
            controlador = 2;

        } else if (controlador == 2) {

            char *token2;
            token2 = strtok(buffer, ";");
            while (token2 != NULL) {

                strcpy(shmem->listaUsers[i].nome, token2);
                token2 = strtok(NULL, ";");

                strcpy(shmem->listaUsers[i].password, token2);
                token2 = strtok(NULL, ";");

                shmem->listaUsers[i].saldo = atoi(token2);
                token2 = strtok(NULL, ";");

                if(i==nusers -1){
                    controlador = 3;
                }else{
                    i++;
                }
            }
        } else {
            char *token3;

            token3 = strtok(buffer, ";");
            while (token3 != NULL) {

                
                if (strcmp(token3, auxt) != 0) {
                    strcpy(auxt, token3);
                    c++;
                }

                if (strcmp(shmem->listaMarkets[c].nome, "") == 0) {
                    strcpy(shmem->listaMarkets[c].nome, token3);
                    token3 = strtok(NULL, ";");
                    strcpy(shmem->listaMarkets[c].lista[d].nome, token3);
                    token3 = strtok(NULL, ";");
                    double valor = atof(token3);
                    shmem->listaMarkets[c].lista[d].valor = valor;
                    d++;
                } else {

                    if(strcmp(shmem->listaMarkets[c].nome,token3)==0){
                        token3 = strtok(NULL, ";");
                        strcpy(shmem->listaMarkets[c].lista[d].nome, token3);
                        token3 = strtok(NULL, ";");
                        double valor = atof(token3);
                        shmem->listaMarkets[c].lista[d].valor = valor;
                        d++;
                    }


                }
                if(d == 3){
                    d = 0;
                }
                token3 = strtok(NULL, ";");
            }
        }
    }
    int h=0;
    for(h = 0;h < NUSERS; h++){
        strcpy(shmem->listaUsers[h].market1, shmem->listaMarkets[0].nome);
        strcpy(shmem->listaUsers[h].market2, shmem->listaMarkets[1].nome);
    }
    fclose(config);
    return 0;
}


int addUser(char name[TAM], char pass[TAM], double saldo, char market1[TAM], char market2[TAM]) {

    for (int i = 0; i < NUSERS; i++) {
        if (strcmp(shmem->listaUsers[i].nome, name) == 0 && strcmp(shmem->listaUsers[i].password, pass) == 0) {
            shmem->listaUsers[i].saldo = saldo;
            if (strcmp(market1, shmem->listaMarkets[0].nome) == 0 || strcmp(market1, shmem->listaMarkets[1].nome) == 0) {
                strcpy(shmem->listaUsers[i].market1, market1);
            } else if (strcmp(market2, shmem->listaMarkets[0].nome) == 0 || strcmp(market2, shmem->listaMarkets[1].nome) == 0) {
                strcpy(shmem->listaUsers[i].market2, market2);
            }
            break;
        }

        if (strcmp(shmem->listaUsers[i].nome, "") == 0) {
            strcpy(shmem->listaUsers[i].nome, name);
            strcpy(shmem->listaUsers[i].password, pass);
            shmem->listaUsers[i].saldo = saldo;
            if (strcmp(market1, shmem->listaMarkets[0].nome) == 0 || strcmp(market1, shmem->listaMarkets[1].nome) == 0) {
                strcpy(shmem->listaUsers[i].market1, market1);
            } else if (strcmp(market2, shmem->listaMarkets[0].nome) == 0 || strcmp(market2, shmem->listaMarkets[1].nome) == 0)
                strcpy(shmem->listaUsers[i].market2, market2);
            break;
        }
    }
    return 0;
}

int inicializacao() {

    for (int i = 0; i < NUSERS; i++) {
        strcpy(shmem->listaUsers[i].nome, "");
        strcpy(shmem->listaUsers[i].password, "");
        shmem->listaUsers[i].saldo = 0;
        strcpy(shmem->listaUsers[i].market1, "");
        strcpy(shmem->listaUsers[i].market2, "");
    }
    return 0;
}

int deleteUser(char nome[TAM]) {
    for (int i = 0; i < NUSERS; i++) {
        if (strcmp(nome, shmem->listaUsers[i].nome) == 0) {
            strcpy(shmem->listaUsers[i].nome, "");
            strcpy(shmem->listaUsers[i].password, "");
            shmem->listaUsers[i].saldo = 0;
            strcpy(shmem->listaUsers[i].market1, "");
            strcpy(shmem->listaUsers[i].market2, "");
        }
    }
    return 0;
}

char * listUsers() {

	char *textoAux = malloc(TAM);
	char aux[TAM*3];
	bzero(textoAux,TAM);

    for (int i = 0; i < NUSERS; i++) {
        if (strcmp(shmem->listaUsers[i].nome, "") != 0) {
            sprintf(aux,"Username: %s, Password: %s, Saldo: %f\n", shmem->listaUsers[i].nome, shmem->listaUsers[i].password,shmem->listaUsers[i].saldo);
            strcat(textoAux,aux);
        }
    }

    return textoAux;
}

double compra(int indexUser,char nome[TAM], double preco,int nAcoes){
    int i, j,k = 0;

    for(i = 0; i < NMARKETS;i++){
        for(j = 0; j < NACOES;j++){
            if(strcmp( (shmem -> listaMarkets[i].lista[j].nome),nome ) == 0 && preco >= shmem -> listaMarkets[i].lista[j].valor){
				
				for(k = 0; k < 6; k++){
                    if((strcmp(shmem -> listaUsers[indexUser].listaCarteira[k].nomeAcao,nome) == 0) && (shmem -> listaUsers[indexUser].saldo >= shmem -> listaMarkets[i].lista[j].valor)){
                        shmem -> listaUsers[indexUser].saldo -= shmem -> listaMarkets[i].lista[j].valor;
                        shmem -> listaUsers[indexUser].listaCarteira[k].numAcoes += nAcoes;
                        return shmem -> listaMarkets[i].lista[j].valor;
                    }
                }
				
                for(k = 0; k < 6; k++){
                    if((shmem -> listaUsers[indexUser].listaCarteira[k].nomeAcao[0] == '\0') && (shmem -> listaUsers[indexUser].saldo >= shmem -> listaMarkets[i].lista[j].valor)){
                        shmem -> listaUsers[indexUser].saldo -= shmem -> listaMarkets[i].lista[j].valor;
                        strcpy(shmem -> listaUsers[indexUser].listaCarteira[k].nomeAcao,nome);
                        shmem -> listaUsers[indexUser].listaCarteira[k].numAcoes += nAcoes;
                        return shmem -> listaMarkets[i].lista[j].valor;
                    }
                }
            }
        }
    }
    return 0;
}


void sub_unsub(int i, char splitedBuf[3][TAM]){
    
    int j;
    int counter = 0;

    for(j = 0; j < 2; j++){
            if(strcmp(splitedBuf[j+1], shmem->listaUsers[i].market1) == 0){
                int k;
                for (k = 0; k < 2; k++) {
                    if (strcmp(shmem->listaUsers[i].marketsSub[k], "\0") == 0){
                        strcpy(shmem->listaUsers[i].marketsSub[k], shmem->listaUsers[i].market1);
                        break;
                    }
					
					if (strcmp(shmem->listaUsers[i].marketsSub[k], splitedBuf[j + 1]) == 0){
						bzero(shmem->listaUsers[i].marketsSub[k], TAM);
						break;
					}
					
                }
                counter+=1;
				
            } 
			if(strcmp(splitedBuf[j+1], shmem->listaUsers[i].market2) == 0){
                int k;
                for (k = 0; k < 2; k++) {
                    if (strcmp(shmem->listaUsers[i].marketsSub[k], "\0") == 0){
                        strcpy(shmem->listaUsers[i].marketsSub[k], shmem->listaUsers[i].market2);
                        break;
                    }
					if (strcmp(shmem->listaUsers[i].marketsSub[k], splitedBuf[j + 1]) == 0){
						bzero(shmem->listaUsers[i].marketsSub[k], TAM);
						break;
					}
                }
                counter+=1;
            }
    }
    if (counter == 0) {
        printf("Erro a subscrever/cancelar \n");
    }
	
}


void feed(int i) {
    write(client, MULTICAST_GROUP, strlen(MULTICAST_GROUP) + 1);
    char text[BUFLEN];
    while (1) {
        char *aux= (char*)malloc(1024);
        int j;
		int k;
		int	l;
		bzero(text, BUFLEN);
		
        for (j = 0; j < 2; j++) {
			
            for (l = 0; l < 2; l++){
				
                if(strcmp(shmem->listaUsers[i].marketsSub[j],shmem->listaMarkets[l].nome)==0) {
                    
                    if (strcmp(aux, shmem->listaMarkets[l].nome) != 0){
                        strcpy(aux, shmem->listaMarkets[l].nome);
						
                        for (k = 0; k < 3; k++) {
                            char aux2[BUFLEN];
                            sprintf(aux2, "%s -> %f\n",shmem->listaMarkets[l].lista[k].nome ,shmem->listaMarkets[l].lista[k].valor);
                            strcat(text, aux2);
                        }
                    }
                }
            }
        }
        strcat(text, "\n");
        cnt = sendto(sock, text, sizeof(text), 0, (struct sockaddr *) &multicast, addrlen);
        if (cnt < 0) {
            perror("sendto");
            exit(1);
        }
        sleep(shmem->refreshTime);
    }
    bzero(buf, BUFLEN);
}

double venda(char acao[TAM], int user , int numAcoes ,double price) {
    int i;
	int j; 
	int k;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            if (strcmp(acao, shmem->listaMarkets[i].lista[j].nome) == 0 && shmem->listaMarkets[i].lista[j].valor >= price) {
                for (k = 0; k < 6; k++){
                    if ((strcmp(shmem->listaUsers[user].listaCarteira[k].nomeAcao, acao) == 0) && (shmem->listaUsers[user].saldo >= shmem->listaMarkets[i].lista[j].valor) && (shmem->listaUsers[user].listaCarteira[k].numAcoes >= numAcoes)) {
                        shmem->listaUsers[user].saldo += shmem->listaMarkets[i].lista[j].valor;
                        shmem->listaUsers[user].listaCarteira[k].numAcoes -= numAcoes;
                        return shmem->listaMarkets[i].lista[j].valor;
                    }
                }
            }
        }
    }
    return 0;
}

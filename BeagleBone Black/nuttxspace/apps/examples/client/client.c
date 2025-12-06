// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Para close()
#include <time.h>       // Para el logging de fecha/hora
#include <strings.h>    // Para strcasecmp() (sensible a mayúsculas/minúsculas)

// ---- Includes de Red (POSIX) ----
#include <sys/types.h>
#include <sys/socket.h> // Para socket(), connect(), send(), recv()
#include <netinet/in.h> // Para struct sockaddr_in, htons()
#include <arpa/inet.h>  // Para inet_pton()
#include <netdb.h>      // Para gethostbyname() (aunque no se usa aquí, es relevante)

// ---- Constantes ----
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_PORT 3001
#define DEFAULT_PROTOCOL "TCP"
#define BUFFER_SIZE 1024

// ---- Prototipos de Funciones ----
void parseArgs(int argc, char *argv[], const char **serverIp, int *port, const char **protocol);
void logMessage(const char *direction, const char *host, const char *socketType, const char *protocol, const char *description);
void startTcpClient(const char *serverIp, int port);
void startUdpClient(const char *serverIp, int port);

int client_main(int argc, char *argv[]) {
    const char *serverIp = DEFAULT_SERVER_IP;
    int port = DEFAULT_PORT;
    const char *protocol = DEFAULT_PROTOCOL;

    parseArgs(argc, argv, &serverIp, &port, &protocol);

    printf("Iniciando cliente con: Server=%s, Port=%d, Protocol=%s\n", serverIp, port, protocol);

    if (strcasecmp(protocol, "TCP") == 0) {
        startTcpClient(serverIp, port);
    } else if (strcasecmp(protocol, "UDP") == 0) {
        startUdpClient(serverIp, port);
    } else {
        fprintf(stderr, "Protocolo inválido especificado. Use TCP o UDP.\n");
        return 1;
    }

    return 0;
}

void startTcpClient(const char *serverIp, int port) {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char userInput[BUFFER_SIZE];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear el socket TCP");
        exit(EXIT_FAILURE);
    }

 
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); 

   
    if (inet_pton(AF_INET, serverIp, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Dirección IP inválida o no soportada: %s\n", serverIp);
        close(sock);
        exit(EXIT_FAILURE);
    }


    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error de conexión TCP");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Conectado al servidor TCP en %s:%d\n", serverIp, port);

   
    do {
        printf("Ingrese la operación a realizar: ");

        if (fgets(userInput, BUFFER_SIZE, stdin) == NULL) {
            break; 
        }
        
      
        userInput[strcspn(userInput, "\n")] = 0;

        send(sock, userInput, strlen(userInput), 0);
        logMessage("<", "localhost", "client", "TCP", "request");

     
        if (strcasecmp(userInput, "EXIT") == 0) {
            break;
        }


        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(sock, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            logMessage(">", serverIp, "server", "TCP", "response");
            printf("Server response: %s\n", buffer);
        } else if (valread == 0) {
            printf("Servidor cerró la conexión\n");
            break;
        } else {
            perror("Error de lectura (read)");
            break;
        }
    } while (1);

    close(sock);
    printf("Conexión TCP cerrada.\n");
}

void startUdpClient(const char *serverIp, int port) {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char userInput[BUFFER_SIZE];

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear el socket UDP");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, serverIp, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Dirección IP inválida o no soportada: %s\n", serverIp);
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    printf("Listo para enviar paquetes UDP a %s:%d\n", serverIp, port);


    do {
        printf("Ingrese la operación a realizar: ");
        if (fgets(userInput, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        userInput[strcspn(userInput, "\n")] = 0; 


        sendto(sock, userInput, strlen(userInput), 0,
               (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
        logMessage("<", "localhost", "client", "UDP", "request");

        if (strcasecmp(userInput, "EXIT") == 0) {
            break;
        }

      
        memset(buffer, 0, BUFFER_SIZE);
        socklen_t server_addr_len = sizeof(serv_addr); 
        int valread = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                               (struct sockaddr *)&serv_addr, &server_addr_len);
        
        if (valread > 0) {
            logMessage(">", serverIp, "server", "UDP", "response");
            printf("Server response: %s\n", buffer);
        } else {
            perror("Error de lectura (recvfrom)");
            break;
        }
    } while (1);

    close(sock);
    printf("Socket UDP cerrado.\n");
}

// ---- Función de Logging ----

void logMessage(const char *direction, const char *host, const char *socketType, const char *protocol, const char *description) {
    char dateTimeStr[64];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(dateTimeStr, sizeof(dateTimeStr), "%Y/%m/%d %H:%M:%S", t);

    printf("%s %s %s [%s] %s: %s\n",
           direction, host, socketType, dateTimeStr, protocol, description);
    fflush(stdout); 
}

// ---- Función de Análisis de Argumentos ----

void parseArgs(int argc, char *argv[], const char **serverIp, int *port, const char **protocol) {
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 < argc) {

            if (strcmp(argv[i], "server") == 0) {
                *serverIp = argv[i + 1];
            } else if (strcmp(argv[i], "port") == 0) {
                *port = atoi(argv[i + 1]); 
            } else if (strcmp(argv[i], "protocol") == 0) {
                *protocol = argv[i + 1];
            }
        }
    }
}
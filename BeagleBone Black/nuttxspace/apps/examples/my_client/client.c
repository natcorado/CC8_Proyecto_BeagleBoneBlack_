#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>      
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>       
#include <time.h>

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_PORT 3001
#define BUFFER_SIZE 1024

typedef struct {
    char server_ip[100];
    int port;
    char protocol[10];
} ClientParams;

static void log_message(const char *direction, const char *host, const char *socket_type, const char *protocol, const char *description) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date_time[20];
    strftime(date_time, sizeof(date_time), "%Y/%m/%d %H:%M:%S", t);
    printf("%s %s %s [%s] %s: %s\n", direction, host, socket_type, date_time, protocol, description);
}

static void parse_args(int argc, char *argv[], ClientParams *params) {
    // Set defaults
    strcpy(params->server_ip, DEFAULT_SERVER_IP);
    params->port = DEFAULT_PORT;
    strcpy(params->protocol, "UDP");

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 < argc) {
            if (strcasecmp(argv[i], "server") == 0) {
                strncpy(params->server_ip, argv[i + 1], sizeof(params->server_ip) - 1);
            } else if (strcasecmp(argv[i], "port") == 0) {
                params->port = atoi(argv[i + 1]);
            } else if (strcasecmp(argv[i], "protocol") == 0) {
                strncpy(params->protocol, argv[i + 1], sizeof(params->protocol) - 1);
            }
        }
    }
}

static void start_udp_client(const char *server_ip, int port) {
    int sock_fd;
    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("ERROR opening UDP socket");
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_aton(server_ip, &server_addr.sin_addr) == 0) {
        fprintf(stderr, "ERROR, invalid host: %s\n", server_ip);
        close(sock_fd);
        return;
    }

    printf("Ready to send UDP packets to %s:%d\n", server_ip, port);

    char user_input[BUFFER_SIZE];
    do {
        printf("Ingrese la operación a realizar: ");
        if (fgets(user_input, sizeof(user_input), stdin) == NULL) {
            break;
        }

        user_input[strcspn(user_input, "\n")] = 0;

        if (sendto(sock_fd, user_input, strlen(user_input), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
            perror("ERROR sending UDP packet");
            break;
        }
        log_message("<", "localhost", "client", "UDP", "request");

        if (strcasecmp(user_input, "EXIT") == 0) {
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        if (recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL) < 0) {
            perror("ERROR receiving UDP packet");
            break;
        }
        log_message(">", server_ip, "server", "UDP", "response");
        printf("Server response: %s\n", buffer);

    } while (1);

    close(sock_fd);
    printf("UDP client shutting down.\n");
}

int main(int argc, char *argv[]) {
    ClientParams params;
    parse_args(argc, argv, &params);

    if (strcasecmp(params.protocol, "UDP") == 0) {
        start_udp_client(params.server_ip, params.port);
    } else {
        fprintf(stderr, "Invalid protocol specified. Use UDP.\n");
    }

    return 0;
}
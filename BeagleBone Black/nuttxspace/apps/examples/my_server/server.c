// server.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <pthread.h>     
#include <time.h>
#include <ctype.h>       
#include <stdbool.h>     
#include <math.h>        
#include <strings.h>     

// Function to calculate checksum
static unsigned short calculate_checksum(unsigned short *buf, int nwords) {
    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

#define DEFAULT_PORT 3001
#define BUFFER_SIZE 1024
#define MAX_TCP_CLIENTS 10

typedef struct {
    int port;
    char protocol[10];
} ServerParams;

static void log_message(const char *direction, const char *host, const char *socket_type, const char *protocol, const char *description) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date_time[20];
    strftime(date_time, sizeof(date_time), "%Y/%m/%d %H:%M:%S", t);
    printf("%s %s %s [%s] %s: %s\n", direction, host, socket_type, date_time, protocol, description);
}

static void parse_args(int argc, char *argv[], ServerParams *params) {
    params->port = DEFAULT_PORT;
    strcpy(params->protocol, "UDP");

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 < argc) {
            if (strcasecmp(argv[i], "port") == 0) {
                params->port = atoi(argv[i + 1]);
            } else if (strcasecmp(argv[i], "protocol") == 0) {
                strncpy(params->protocol, argv[i + 1], sizeof(params->protocol) - 1);
            }
        }
    }
}

typedef struct {
    int pos;
    int ch;
    const char *str;
    bool error;
} Parser;

void nextChar(Parser *p) {
    p->ch = (++p->pos < (int)strlen(p->str)) ? p->str[p->pos] : -1;
}

bool eat(Parser *p, int charToEat) {
    while (isspace(p->ch)) nextChar(p);
    if (p->ch == charToEat) {
        nextChar(p);
        return true;
    }
    return false;
}

double parseFactor(Parser *p);

double parseTerm(Parser *p) {
    double x = parseFactor(p);
    if (p->error) return NAN;
    for (;;) {
        if (eat(p, '*')) x *= parseFactor(p);
        else if (eat(p, '/')) x /= parseFactor(p);
        else return x;
        if (p->error) return NAN;
    }
}

double parseExpression(Parser *p) {
    double x = parseTerm(p);
    if (p->error) return NAN;
    for (;;) {
        if (eat(p, '+')) x += parseTerm(p);
        else if (eat(p, '-')) x -= parseTerm(p);
        else return x;
        if (p->error) return NAN;
    }
}

double parseFactor(Parser *p) {
    if (eat(p, '+')) return parseFactor(p);
    if (eat(p, '-')) return -parseFactor(p);

    double x;
    int startPos = p->pos;
    if (eat(p, '(')) {
        x = parseExpression(p);
        if (!eat(p, ')')) {
            p->error = true;
            return NAN;
        }
    } else if ((p->ch >= '0' && p->ch <= '9') || p->ch == '.') {
        while ((p->ch >= '0' && p->ch <= '9') || p->ch == '.') nextChar(p);
        char *numStr = strndup(p->str + startPos, p->pos - startPos);
        if (numStr == NULL) {
            p->error = true;
            return NAN;
        }
        x = atof(numStr);
        free(numStr);
    } else {
        p->error = true;
        return NAN;
    }
    return x;
}

double simpleEval(const char *expr) {
    Parser p = {-1, -1, expr, false};
    nextChar(&p);
    double x = parseExpression(&p);
    if (p.error || (size_t)p.pos < strlen(expr)) {
        return NAN; // Return NaN on error
    }
    return x;
}

char* evaluateExpression(const char *expression) {
    static __thread char result_buffer[256];

    double result = simpleEval(expression);
    if (isnan(result)) {
        strcpy(result_buffer, "Error: Invalid expression");
    } else {
        snprintf(result_buffer, sizeof(result_buffer), "%.5g", result);
    }
    return result_buffer;
}

void start_udp_server(int port) {
    int sock_fd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);

    sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sock_fd < 0) {
        perror("ERROR opening UDP socket");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ERROR on binding");
        close(sock_fd);
        return;
    }

    printf("UDP Server is listening on port %d\n", port);

    while (1) {
        int bytes_read = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&client_addr, &client_len);
        if (bytes_read < 0) {
            perror("ERROR on raw recvfrom");
            continue;
        }

        struct iphdr *iph = (struct iphdr *)buffer;
        // The UDP header is located after the IP header (iph->ihl * 4 bytes)
        struct udphdr *udph = (struct udphdr *)(buffer + iph->ihl * 4);
        // The data payload is after the UDP header
        char *data = buffer + iph->ihl * 4 + sizeof(struct udphdr);
        
        // Calculate the length of the data payload from the UDP header
        int data_len = ntohs(udph->len) - sizeof(struct udphdr);
        if (data_len < 0) {
            data_len = 0; // Prevent negative length
        }
        // Null-terminate the received data to treat it as a string
        data[data_len] = '\0';

        // The client's address information is in the IP and UDP headers
        client_addr.sin_addr.s_addr = iph->saddr;
        client_addr.sin_port = udph->source;
        
        char *client_ip = inet_ntoa(client_addr.sin_addr);
        log_message(">", client_ip, "client", "RAW/UDP", data);

        if (strcasecmp(data, "EXIT") == 0) {
            printf("EXIT command received. Shutting down raw server.\n");
            break;
        }

        if (strncasecmp(data, "ECHO ", 5) == 0) {
            strcpy(response, data + 5);
        } else {
            strcpy(response, evaluateExpression(data)); 
        }

        // Construct the raw response packet
        char datagram[BUFFER_SIZE];
        struct iphdr *res_iph = (struct iphdr *)datagram;
        struct udphdr *res_udph = (struct udphdr *)(datagram + sizeof(struct iphdr));
        char *res_data = datagram + sizeof(struct iphdr) + sizeof(struct udphdr);
        strcpy(res_data, response);

        // IP Header
        res_iph->ihl = 5;
        res_iph->version = 4;
        res_iph->tos = 0;
        res_iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + strlen(res_data));
        res_iph->id = htonl(rand()); // Random ID
        res_iph->frag_off = 0;
        res_iph->ttl = 255;
        res_iph->protocol = IPPROTO_UDP;
        res_iph->check = 0; // Set to 0 before checksum calculation
        res_iph->saddr = server_addr.sin_addr.s_addr; // Source is me
        res_iph->daddr = client_addr.sin_addr.s_addr; // Destination is client

        // UDP Header
        res_udph->source = server_addr.sin_port; // Source is me
        res_udph->dest = client_addr.sin_port;   // Destination is client
        res_udph->len = htons(sizeof(struct udphdr) + strlen(res_data));
        res_udph->check = 0; // UDP checksum is optional

        // Calculate IP checksum
        res_iph->check = calculate_checksum((unsigned short *)datagram, sizeof(struct iphdr));

        if (sendto(sock_fd, datagram, ntohs(res_iph->tot_len), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            perror("ERROR on raw sendto");
        }
        log_message("<", "localhost", "server", "RAW/UDP", response);
    }

    close(sock_fd);
}

int main(int argc, char *argv[]) {
    ServerParams params;
    parse_args(argc, argv, &params);

    printf("Server starting with protocol UDP on port %d\n", params.port);

	if (strcasecmp(params.protocol, "UDP") == 0) {
        start_udp_server(params.port);
    } else {
        fprintf(stderr, "Invalid protocol specified. Use UDP.\n");
    }

    return 0;
}
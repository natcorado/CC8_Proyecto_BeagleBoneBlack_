#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define IP_HDRINCL 3

// Function to calculate checksum
static unsigned short calculate_checksum(unsigned short *buf, int nwords) {
    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

void *listener(void *arg) {
    int s = *(int*)arg;
    char buffer[65536];
    struct sockaddr_in sin;
    socklen_t sin_len = sizeof(sin);

    while (1) {
        int data_size = recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr *)&sin, &sin_len);
        if (data_size < 0) {
            continue;
        }

        struct iphdr *iph = (struct iphdr*)buffer;
        if (iph->protocol == IPPROTO_UDP) {
            struct udphdr *udph = (struct udphdr*)(buffer + iph->ihl * 4);
            char *data = buffer + iph->ihl * 4 + sizeof(struct udphdr);
            
            time_t now = time(0);
            struct tm *tm_info = localtime(&now);
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%d/%m/%Y %H:%M:%S", tm_info);

            printf("> %s server [%s] UDP: %s\n", inet_ntoa(sin.sin_addr), time_str, data);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // Hardcode destination for loopback testing
    char *dest_ip = "127.0.0.1";
    int dest_port = 3001;
    printf("Targeting loopback server at %s:%d\n", dest_ip, dest_port);

    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (s < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int one = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt(IP_HDRINCL) failed");
        return 1;
    }

    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, listener, &s) != 0) {
        perror("pthread_create failed");
        return 1;
    }

    char message[1024];
    while (1) {
        printf("Enter message: ");
        if (fgets(message, sizeof(message), stdin) == NULL) {
            break;
        }
        message[strcspn(message, "\n")] = 0; // Remove newline

        char datagram[4096];
        struct iphdr *iph = (struct iphdr *)datagram;
        struct udphdr *udph = (struct udphdr *)(datagram + sizeof(struct iphdr));
        char *data = datagram + sizeof(struct iphdr) + sizeof(struct udphdr);
        strcpy(data, message);

        // IP Header
        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + strlen(data));
        iph->id = htonl(54321);
        iph->frag_off = 0;
        iph->ttl = 255;
        iph->protocol = IPPROTO_UDP;
        iph->check = 0; // Set to 0 before calculating checksum
        iph->saddr = inet_addr("127.0.0.1"); // Source IP, you might want to change this
        iph->daddr = inet_addr(dest_ip);

        iph->check = calculate_checksum((unsigned short *)datagram, iph->tot_len >> 1);

        // UDP Header
        srand(time(NULL));
        int source_port = 1024 + (rand() % (65535 - 1024));
        udph->source = htons(source_port);
        udph->dest = htons(dest_port);
        udph->len = htons(8 + strlen(data));
        udph->check = 0; // Checksum is optional for UDP

        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(dest_port);
        sin.sin_addr.s_addr = inet_addr(dest_ip);

        if (sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            perror("sendto failed");
        } else {
            time_t now = time(0);
            struct tm *tm_info = localtime(&now);
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%d/%m/%Y %H:%M:%S", tm_info);
            printf("< %s client [%s] UDP: %s\n", dest_ip, time_str, message);
        }

        if (strcmp(message, "EXIT") == 0) {
            break;
        }
    }

    close(s);
    return 0;
}

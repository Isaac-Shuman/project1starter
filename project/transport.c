#include "consts.h"
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>

/*
 * the following variables are only informational,
 * not necessarily required for a valid implementation.
 * feel free to edit/remove.
 */
int state = 0;           // Current state for handshake
int our_send_window = 0; // Total number of bytes in our send buf
int their_receiving_window = MIN_WINDOW;   // Receiver window size
int our_max_receiving_window = MIN_WINDOW; // Our max receiving window
int our_recv_window = 0;                   // Bytes in our recv buf
int dup_acks = 0;        // Duplicate acknowledgements received
uint32_t ack = 0;        // Acknowledgement number
uint32_t seq = 0;        // Sequence number
uint32_t last_ack = 0;   // Last ACK number to keep track of duplicate ACKs
bool pure_ack = false;  // Require ACK to be sent out
packet* base_pkt = NULL; // Lowest outstanding packet to be sent out

buffer_node* recv_buf = 
    NULL; // Linked list storing out of order received packets
buffer_node* send_buf =
    NULL; // Linked list storing packets that were sent but not acknowledged

ssize_t (*input)(uint8_t*, size_t); // Get data from layer
void (*output)(uint8_t*, size_t);   // Output data from layer

struct timeval start; // Last packet sent at this time
struct timeval now;   // Temp for current time

void init_buffer(buffer_node** node);
packet* get_last_packet(void);
void update_sending_buffer(uint16_t ack_given);

// Get data from standard input / make handshake packets
packet* get_data() {
    packet* pkt = &recv_buf->pkt;
    switch (state) {
    case SERVER_AWAIT:
    case CLIENT_AWAIT:
    case CLIENT_START:
    case SERVER_START:
    default: {
    }
    }

    if (our_send_window < their_receiving_window) {
        uint16_t length;
        length = input(pkt->payload, their_receiving_window - our_send_window);
        pkt->length = length;
        pkt->ack = ack;
        pkt->seq = seq;
        
        //update sending state
        seq += length;
        our_send_window += length;
        assert(our_send_window <= their_receiving_window);
        
        if (length > 0) {
            base_pkt = pkt;
            return pkt;
        }
        else
            return NULL;
    }
    else
        return NULL;
}

// Process data received from socket
void recv_data(packet* pkt) {
    switch (state) {
    case CLIENT_START:
    case SERVER_START:
    case SERVER_AWAIT:
    case CLIENT_AWAIT: {
    }
    default: {
    }
    }

    if (pkt->seq == ack) { //if this is a new packet
        //update recieving state
        ack += pkt->length;

        output(pkt->payload, pkt->length);
    }
    update_sending_buffer(pkt->ack);
}

// Main function of transport layer; never quits
void listen_loop(int sockfd, struct sockaddr_in* addr, int initial_state,
                 ssize_t (*input_p)(uint8_t*, size_t),
                 void (*output_p)(uint8_t*, size_t)) {

    // Set initial state (whether client or server)
    state = initial_state;

    // Set input and output function pointers
    input = input_p;
    output = output_p;

    // Set socket for nonblocking
    int flags = fcntl(sockfd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, flags);
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &(int) {1}, sizeof(int));

    // Set initial sequence number
    uint32_t r;
    int rfd = open("/dev/urandom", 'r');
    read(rfd, &r, sizeof(uint32_t));
    close(rfd);
    srand(r);
    seq = (rand() % 10) * 100 + 100;

    // Setting timers
    gettimeofday(&now, NULL);
    gettimeofday(&start, NULL);

    // Create buffer for incoming data
    char buffer[sizeof(packet) + MAX_PAYLOAD] = {0};
    packet* pkt = (packet*) &buffer;
    socklen_t addr_size = sizeof(struct sockaddr_in);

    //Isaac: Initialize linked lists
    init_buffer(&send_buf);
    init_buffer(&recv_buf);

    // Start listen loop
    while (true) {
        memset(buffer, 0, sizeof(packet) + MAX_PAYLOAD);
        // Get data from socket
        int bytes_recvd = recvfrom(sockfd, &buffer, sizeof(buffer), 0,
                                   (struct sockaddr*) addr, &addr_size);
        // If data, process it
        if (bytes_recvd > 0) {
            //print_diag(pkt, RECV);
            recv_data(pkt);
        }

        packet* tosend = get_data();
        // Data available to send
        if (tosend != NULL) {
            tosend->ack = ack;
            sendto(sockfd, tosend, sizeof(packet) + tosend->length, 0, (const struct sockaddr*) addr, addr_size);
        }
        // Received a packet and must send an ACK
        else if (pure_ack) {
            packet* ack_pack = pkt;
            ack_pack->length = 0;
            ack_pack->ack = ack;
            ack_pack->seq = seq;
            sendto(sockfd, ack_pack, sizeof(packet) + ack_pack->length, 0, (const struct sockaddr*) addr, addr_size);
        }

        // Check if timer went off
        gettimeofday(&now, NULL);
        if (TV_DIFF(now, start) >= RTO && base_pkt != NULL) {
            packet* toresend = get_last_packet();
            toresend->ack = ack;
            sendto(sockfd, toresend, sizeof(packet) + toresend->length, 0, (const struct sockaddr*) addr, addr_size);
        }
        // Duplicate ACKS detected
        else if (dup_acks == DUP_ACKS && base_pkt != NULL) {
        }
        // No data to send, so restart timer
        else if (base_pkt == NULL) {
            gettimeofday(&start, NULL);
        }
    }
}

void init_buffer(buffer_node** node) {
    *node = (buffer_node *) malloc(sizeof(buffer_node));
}

packet* get_last_packet(void) {
    return base_pkt;
}

void update_sending_buffer(uint16_t ack_given) {
    if (base_pkt != NULL) {
        if (base_pkt->seq + base_pkt->length <= ack_given) {
            our_send_window -= base_pkt->length;
            assert(our_send_window >= 0);
            base_pkt = NULL;
        }
    }
}
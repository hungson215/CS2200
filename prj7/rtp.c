#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "rtp.h"

/* GIVEN Function:
 * Handles creating the client's socket and determining the correct
 * information to communicate to the remote server
 */
CONN_INFO* setup_socket(char* ip, char* port){
	struct addrinfo *connections, *conn = NULL;
	struct addrinfo info;
	memset(&info, 0, sizeof(struct addrinfo));
	int sock = 0;

	info.ai_family = AF_INET;
	info.ai_socktype = SOCK_DGRAM;
	info.ai_protocol = IPPROTO_UDP;
	getaddrinfo(ip, port, &info, &connections);

	/*for loop to determine corr addr info*/
	for(conn = connections; conn != NULL; conn = conn->ai_next){
		sock = socket(conn->ai_family, conn->ai_socktype, conn->ai_protocol);
		if(sock <0){
			if(DEBUG)
				perror("Failed to create socket\n");
			continue;
		}
		if(DEBUG)
			printf("Created a socket to use.\n");
		break;
	}
	if(conn == NULL){
		perror("Failed to find and bind a socket\n");
		return NULL;
	}
	CONN_INFO* conn_info = malloc(sizeof(CONN_INFO));
	conn_info->socket = sock;
	conn_info->remote_addr = conn->ai_addr;
	conn_info->addrlen = conn->ai_addrlen;
	return conn_info;
}

void shutdown_socket(CONN_INFO *connection){
	if(connection)
		close(connection->socket);
}

/* 
 * ===========================================================================
 *
 *			STUDENT CODE STARTS HERE. PLEASE COMPLETE ALL FIXMES
 *
 * ===========================================================================
 */


/*
 *  Returns a number computed based on the data in the buffer.
 */
static int checksum(char *buffer, int length){

	/*  ----  FIXME  ----
	 *
	 *  The goal is to return a number that is determined by the contents
	 *  of the buffer passed in as a parameter.  There a multitude of ways
	 *  to implement this function.  For simplicity, simply sum the ascii
	 *  values of all the characters in the buffer, and return the total.
	 */ 
    int i = 0;
    int result = 0;
    while (i < length) {
        result += buffer[i];
        i++;
    }
    return result;
}

/*
 *  Converts the given buffer into an array of PACKETs and returns
 *  the array.  The value of (*count) should be updated so that it 
 *  contains the length of the array created.
 */
static PACKET* packetize(char *buffer, int length, int *count){

	/*  ----  FIXME  ----
	 *  The goal is to turn the buffer into an array of packets.
	 *  You should allocate the space for an array of packets and
	 *  return a pointer to the first element in that array.  Each 
	 *  packet's type should be set to DATA except the last, as it 
	 *  should be LAST_DATA type. The integer pointed to by 'count' 
	 *  should be updated to indicate the number of packets in the 
	 *  array.
	 */
    int packets = length / MAX_PAYLOAD_LENGTH + (length % MAX_PAYLOAD_LENGTH == 0 ? 0 : 1);
    
    PACKET* packet =(PACKET*) malloc(sizeof(PACKET) * packets);
    int i = 0;
    
    while (i < packets-1) {
        packet[i].type = DATA;
        packet[i].payload_length = MAX_PAYLOAD_LENGTH;
        memcpy(packet[i].payload, &buffer[i*MAX_PAYLOAD_LENGTH],MAX_PAYLOAD_LENGTH);
        packet[i].checksum = checksum(packet[i].payload,packet[i].payload_length);
        i++;
    }
    packet[i].type = LAST_DATA;
    packet[i].payload_length = length - (packets-1) * MAX_PAYLOAD_LENGTH;  
    memcpy(packet[i].payload, &buffer[(packets-1) * MAX_PAYLOAD_LENGTH], packet[i].payload_length);
    packet[i].checksum = checksum(packet[i].payload,packet[i].payload_length);
    
    *count = packets;
    return packet;
}

/*
 * Send a message via RTP using the connection information
 * given on UDP socket functions sendto() and recvfrom()
 */
int rtp_send_message(CONN_INFO *connection, MESSAGE*msg){
	/* ---- FIXME ----
	 * The goal of this function is to turn the message buffer
	 * into packets and then, using stop-n-wait RTP protocol,
	 * send the packets and re-send if the response is a NACK.
	 * If the response is an ACK, then you may send the next one
	 */
    int packets = 0;
    PACKET* packet = packetize(msg->buffer, msg->length, &packets);
    PACKET *res_msg = (PACKET*) malloc(sizeof(PACKET));
    
    int i = 0;
    do{        
        sendto(connection->socket,&packet[i], sizeof(PACKET),0,connection->remote_addr,connection->addrlen);
        recvfrom(connection->socket,res_msg,sizeof(PACKET),0,connection->remote_addr,&connection->addrlen);
        if(res_msg->type == ACK){
            i++;
        }        
    }while(i<packets);
    free(packet);
    free(res_msg);
    return 0;
}

/*
 * Receive a message via RTP using the connection information
 * given on UDP socket functions sendto() and recvfrom()
 */
MESSAGE* rtp_receive_message(CONN_INFO *connection){
	/* ---- FIXME ----
	 * The goal of this function is to handle 
	 * receiving a message from the remote server using
	 * recvfrom and the connection info given. You must 
	 * dynamically resize a buffer as you receive a packet
	 * and only add it to the message if the data is considered
	 * valid. The function should return the full message, so it
	 * must continue receiving packets and sending response 
	 * ACK/NACK packets until a LAST_DATA packet is successfully 
	 * received.
	 */
    char* buffer;   
    MESSAGE* msg = (MESSAGE*) malloc(sizeof(MESSAGE));
    msg->length = 0;
    PACKET inc_packet;
    PACKET res_packet;
    int bFlag = 1; 
    
    do{
        recvfrom(connection->socket, &inc_packet,sizeof(PACKET),0,connection->remote_addr,&connection->addrlen);
        //Compare checksum to see if the packet is corrupted
        int packet_checksum = checksum(inc_packet.payload,inc_packet.payload_length);
        
        if(inc_packet.checksum != packet_checksum) {
            res_packet.type = NACK;
            sendto(connection->socket,&res_packet, sizeof(PACKET),0,connection->remote_addr,connection->addrlen);
        //If not then at it to the message
        } else {
            //Add 1 at the end for null terminator
            buffer = (char*) malloc(sizeof(char) * (msg->length + inc_packet.payload_length + 1));
            
            //If msg is not empty copy it to buffer
            if(msg->length != 0){
                memcpy(buffer,msg->buffer, msg->length);
                free(msg->buffer);
            }
            //Copy the packet's message
            memcpy(&buffer[msg->length], inc_packet.payload, inc_packet.payload_length);
            
            msg->length += inc_packet.payload_length;
            msg->buffer = buffer;       
            
            res_packet.type = ACK;
            sendto(connection->socket,&res_packet, sizeof(PACKET),0,connection->remote_addr,connection->addrlen);
            
            //Check if the message has been completely received
            if(inc_packet.type == LAST_DATA) {
                bFlag = 0;
            }
        }
    }while(bFlag);
    msg->buffer[msg->length] = 0; // String in C is null terminator string so we have to set that last char is 0
    return msg; // Memory leak if msg is not freed in the caller
}

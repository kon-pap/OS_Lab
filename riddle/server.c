#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]){
	int sockfd,newsockfd,portno,clilen;
	char buffer[256];
	struct sockaddr_in serv_addr, cli_addr;
	int n;
	char first_num_string[5], string_to_send[5];
	int first_num, num_to_send;

	portno = 49842;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		perror("Failed to create socket");
		return 1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1){
		perror("Failed to bind socket");
		return 1;
	}
	listen(sockfd,5);
	clilen = sizeof(cli_addr);
	newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	if (newsockfd < 0){
		perror("Failed to accept connection");
		return 1;
	}
	bzero(buffer,256);
	n = read(newsockfd,buffer,255);
	if (n < 0){
		perror("Failed to read from socket");
		return 1;
	}
	memcpy(first_num_string,&buffer[12],6);
	// first_num_string[4] = '\0';
	first_num = atoi(first_num_string);
	num_to_send = first_num + 1;
	sprintf(string_to_send,"%d",num_to_send);
	n = write(newsockfd,string_to_send,6);
	// printf("Here's what I extracted: %s\n",first_num_string);
	close(sockfd);
	return 0;
}

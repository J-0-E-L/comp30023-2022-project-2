/* Socket setup based on the examples in "Beej's Guide to Network Programming": https://beej.us/guide/bgnet/html/ */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BACKLOG 5

int main(int argc, char **argv) {
	struct addrinfo hints, *res;

	/* Specify interface options */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	/* Get interfaces which adhere to our options */
	int status;
	if ((status = getaddrinfo(NULL, "8080", &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return 1;
	}

	/* Bind to the first interface we can */
	struct addrinfo *p;
	int sockfd;
	for (p = res; p; p = p->ai_next) {
		/* Try to get a suitable socket for this interface */
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}
		
		/* Enable immediate reuse of the interface and port */
		int enable = 1;
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1) {
			perror("setsockopt");
			close(sockfd);
			freeaddrinfo(res);
			return 1;
		}

		/* Try to bind the socket to a port */
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("bind");
			close(sockfd);
			continue;
		}

		/* We have a valid socket */
		break;
	}
	/* Clean up */
	freeaddrinfo(res);
	
	/* None of the interfaces worked */
	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		return 1;
	}
	
	/* Get ready to receive connections */
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		close(sockfd);
		return 1;
	}

	/* Accept incoming connections */
	int newsockfd;
	struct sockaddr_storage addr;
	socklen_t socklen = sizeof(struct sockaddr_storage);
	if ((newsockfd = accept(sockfd, (struct sockaddr *)&addr, &socklen)) == -1) {
		perror("accept");
		close(sockfd);
		return 1;
	}
	
	close(sockfd);
	return 0;
}

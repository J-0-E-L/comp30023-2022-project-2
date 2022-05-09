/* Socket setup based on the examples in "Beej's Guide to Network Programming": https://beej.us/guide/bgnet/html/ */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BACKLOG 10 
#define BUFFER_LEN 256
#define TOKEN_LEN 64

struct sockwrap {
	int sockfd, buff_len, start, n;
	char *buff;
};

struct sockwrap *init_sockwrap(int sockfd, int buff_len);
int freesockwrap(struct sockwrap *sockw);
int nextchar(struct sockwrap *sockw, char *c);
int nexttoken(struct sockwrap *sockw, char **s, int token_len, char del);

int main(int argc, char **argv) {
	/* Validate arguments */
	if (argc != 4) {
		fprintf(stderr, "usage: %s [protocol number] [port number] [web root]\n", argv[0]);
		return 1;
	}
	// TODO: validate arguments... helper function?
	char *protocol = argv[1], *port = argv[2], *root = argv[3];
	struct addrinfo hints, *res;

	/* Specify interface options */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	/* Get interfaces which adhere to our options */
	int status;
	if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
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
	
	struct sockwrap *sockw = init_sockwrap(newsockfd, BUFFER_LEN);
	
	while (1) {
		char *s;
		int n = nexttoken(sockw, &s, TOKEN_LEN, '\n');
		if (n == 1) {
			printf("%s\n", s);
			free(s);
			fflush(stdout);
		} else {
			break;
		}
	}

	freesockwrap(sockw);
	close(sockfd);
	return 0;
}

struct sockwrap *init_sockwrap(int sockfd, int buff_len) {
	struct sockwrap *sockw = (struct sockwrap *)malloc(sizeof(struct sockwrap));
	if (sockw == NULL) {
		return NULL;
	}

	char *buff = (char *)malloc(buff_len * sizeof(char));
	if (buff == NULL) {
		free(sockw);
		return NULL;
	}

	sockw->sockfd = sockfd;
	sockw->buff_len = buff_len;
	sockw->start = 0;
	sockw->n = 0;
	sockw->buff = buff;
	return sockw;
}

int freesockwrap(struct sockwrap *sockw) {
	int sockfd = sockw->sockfd;
	free(sockw->buff);
	free(sockw);
	return sockfd;
}

/* -1: error, 0: disconnect, 1: character received */
int nextchar(struct sockwrap *sockw, char *c) {
	/* Get more data when the buffer is empty */
	if (sockw->n == 0) {
		int n = recv(sockw->sockfd, sockw->buff, sockw->buff_len, 0);
		if (n == -1 || n == 0) { 
			return n;
		}
		sockw->start = 0;
		sockw->n = n;
	}
	
	/* Return the first character in the buffer */
	*c = sockw->buff[sockw->start];
	sockw->start++; // TODO: should we leave this index out of range if we read the entire buffer?
	sockw->n--;
	return 1;
}

/* -1: error, 0: disconnect, 1: token received, 2: token too large */
int nexttoken(struct sockwrap *sockw, char **s, int token_len, char del) { // TODO: support multi-characer delimeters
	char *t = (char *)malloc((token_len + 1) * sizeof(char)); // TODO: use a buffer instead of allocating each time
	if (t == NULL) {
		return -1;
	}
	
	int i, found = 0;
	char c;
	for (i = 0; i < token_len + 1; i++) {
		/* Get the next character */
		int n = nextchar(sockw, &c);
		if (n == -1 || n == 0) {
			return n;
		}

		/* Found the delimeter */
		if (c == del) {
			t[i] = '\0';
			found = 1;
			break;
		}
		/* Still in the same token */
		t[i] = c;
	}

	/* The token received is too big */
	if (!found) {
		free(t);
		return 2;
	}

	t = (char *)realloc(t, (i + 1) * sizeof(char));
	*s = t;
	return 1;
}

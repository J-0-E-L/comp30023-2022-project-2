/* Socket setup based on the examples in "Beej's Guide to Network Programming": https://beej.us/guide/bgnet/html/ */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define IMPLEMENTS_IPV6
// #define MULTITHREADED

#define BACKLOG 10 
#define BUFFER_LEN 256
#define TOKEN_LEN 64

struct sockwrap {
	int sockfd, buff_len, start, n;
	char *buff;
};

struct sockwrap *initsockwrap(int sockfd, int buff_len);
int freesockwrap(struct sockwrap *sockw);
int nextchar(struct sockwrap *sockw, char *c);
int nexttoken(struct sockwrap *sockw, char *token, int token_len, int *token_n, char *del);
int sendfile(struct sockwrap *sockw, FILE *fp);

int getargs(int argc, char **argv, char **protocol, char **port, char **root);
int getlistensock(int *sockfd, char *protocol, char *port);

int validpath(char *path);
char *trap(char *root, char *path);
char *getmime(char *path);

int main(int argc, char **argv) {
	/* Read and verify command line arguments */
	char *protocol, *port, *root;
	if (getargs(argc, argv, &protocol, &port, &root) == 1) {
		return 1;
	}

	/* Produce a listening socket */
	int sockfd;
	if (getlistensock(&sockfd, protocol, port) == 1) {
		return 1;
	}

	while (1) {
		/* Accept incoming connections */
		int newsockfd;
		struct sockaddr_storage addr;
		socklen_t socklen = sizeof(struct sockaddr_storage);
		if ((newsockfd = accept(sockfd, (struct sockaddr *)&addr, &socklen)) == -1) {
			perror("accept");
			close(sockfd);
			return 1;
		}
		
		struct sockwrap *sockw = initsockwrap(newsockfd, BUFFER_LEN);

		/* Read in the request line */
		char method[TOKEN_LEN + 1], file[TOKEN_LEN + 1], version[TOKEN_LEN + 1];
		char *tokens[3] = {method, file, version}, *dels[3] = {" ", " ", "\r\n"}; // tokens and dels must be the same length
		int tokens_len = 3;
		for (int i = 0; i < tokens_len; i++) {
			int token_n;
			if (nexttoken(sockw, tokens[i], TOKEN_LEN, &token_n, dels[i]) == -1) {
				perror("nexttoken");
				freesockwrap(sockw);
				close(newsockfd);
				close(sockfd);
				return 1;
			}
			tokens[i][token_n] = '\0';
		}

		// TODO: at least look at the headers
	
		/* Serve the request */
		char *msg;
		if (strcmp(method, "GET") != 0 || validpath(file) == 0 || strcmp(version, "HTTP/1.0") != 0) {
			msg = "HTTP/1.0 400 Invalid request\r\n\r\n";
			send(sockw->sockfd, msg, strlen(msg), 0);
		} else {
			char *trapped = trap(root, file);
			if (trapped == NULL) {
				freesockwrap(sockw);
				close(newsockfd);
				close(sockfd);
				return 1;
			}

			FILE *fp = fopen(trapped, "r");
			if (fp == NULL) { // TODO: implement code 403 (read permissions... what level should the program need?)
				msg = "HTTP/1.0 404 File not found\r\n\r\n";
				send(sockw->sockfd, msg, strlen(msg), 0); // TODO: handle these send errors
			} else {
				msg = "HTTP/1.0 200 OK\r\n";
				send(sockw->sockfd, msg, strlen(msg), 0);

				msg = "Content-Type: "; // TODO: some system of sending headers in general?
				send(sockw->sockfd, msg, strlen(msg), 0);
				msg = getmime(trapped);
				free(trapped);
				send(sockw->sockfd, msg, strlen(msg), 0);
				free(msg);
				msg = "\r\n\r\n";
				send(sockw->sockfd, msg, strlen(msg), 0);

				sendfile(sockw, fp);
				fclose(fp);
			}
		}

		freesockwrap(sockw);
		close(newsockfd);
	}
	close(sockfd);
	return 0;
}

struct sockwrap *initsockwrap(int sockfd, int buff_len) {
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
int nexttoken(struct sockwrap *sockw, char *token, int token_len, int *token_n, char *del) {
	int buff_len = strlen(del), buff_n = 0;
	char *buff = (char *)malloc(buff_len * sizeof(char)); // TODO: should the size of del be limited to TOKEN_LEN? if so use a static buffer
	if (buff == NULL) {
		return -1;
	}
	
	*token_n = 0;
	while (1) {
		int n = nextchar(sockw, buff + buff_n);
		if (n == -1 || n == 0) {
			return n;
		}
		buff_n++;

		if (buff[buff_n - 1] == del[buff_n - 1]) {
			/* Found the delimeter */
			if (buff_n == buff_len) {
				free(buff);
				return 1;
			}
		} else {
			/* The received token is too long */
			if (*token_n + buff_n > token_len) {
				free(buff);
				return 2;
			}
			/* Move the buffer into the token */
			for (int i = 0; i < buff_n; i++) {
				token[*token_n + i] = buff[i];
			}
			*token_n += buff_n;
			buff_n = 0;
		}
	}
	/* We should never get here */
	return -1;
}

/* -1: error, 1: file sent */
int sendfile(struct sockwrap *sockw, FILE *fp) {
	char buff[BUFFER_LEN]; // TODO: should sockwrap contain an out buffer?
	int nread;
	while ((nread = fread(buff, sizeof(char), BUFFER_LEN, fp)) > 0) { // TODO: is it dangerous to use a 0 character read to indicate EOF?
		int ntosend = nread;
		/* Make sure everything is sent */
		while (ntosend > 0) {
			int nsent = send(sockw->sockfd, buff, nread, 0);
			if (nsent == -1) {
				perror("sendfile");
				return -1;
			}
			ntosend -= nsent;
		}
	}
	// TODO: is there a way of detecting errors in fread?
	return 1;
}

int getargs(int argc, char **argv, char **protocol, char **port, char **root) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s [protocol number] [port number] [web root]\n", argv[0]);
		return 1;
	}

	*protocol = argv[1];
	*port = argv[2];
	*root = argv[3];
	
	if (strcmp(*protocol, "6") != 0 && strcmp(*protocol, "4") != 0) {
		fprintf(stderr, "protocol number must be 4 or 6\n");
		return 1;
	}
	
	int t = atoi(*port); // TODO: do not use atoi
	if (t < 0 || t >= 65536) {
		fprintf(stderr, "port must be between 0 and 65535 (inclusive)\n");
		return 1;
	}

	if (!validpath(*root)) { // TODO: does the folder exist?
		fprintf(stderr, "web root must be a valid path\n");
		return 1;
	}

	return 0;
}

int getlistensock(int *sockfd, char *protocol, char *port) {
	struct addrinfo hints, *res;

	/* Specify interface options */
	memset(&hints, 0, sizeof(struct addrinfo));
	// hints.ai_family = AF_UNSPEC;
	hints.ai_family = (strcmp(protocol, "6") == 0) ? AF_INET6 : AF_INET;
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
	int newsockfd;
	for (p = res; p; p = p->ai_next) {
		/* Try to get a suitable socket for this interface */
		if ((newsockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}
		
		/* Enable immediate reuse of the interface and port */
		int enable = 1;
		if (setsockopt(newsockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1) {
			perror("setsockopt");
			close(newsockfd);
			freeaddrinfo(res);
			return 1;
		}

		/* Try to bind the socket to a port */
		if (bind(newsockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("bind");
			close(newsockfd);
			continue;
		}

		/* We have a valid socket */
		break;
	}
	freeaddrinfo(res);
	
	/* None of the interfaces worked */
	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		return 1;
	}
	
	/* Get ready to receive connections */
	if (listen(newsockfd, BACKLOG) == -1) {
		perror("listen");
		close(newsockfd);
		return 1;
	}
	
	/* Expose the listening socket */
	*sockfd = newsockfd;
	return 0;
}

int validpath(char *path) { // TODO: implement this
	return 1;
}

char *trap(char *root, char *path) { // TODO: implement this
	char *out = (char *)malloc((strlen(root) + strlen(path) + 1) * sizeof(char));
	strcpy(out, root);
	return strcat(out, path);
}

char *getmime(char *path) { // TODO: should this need to receive a valid path?
	if (path == NULL) {
		return NULL;
	}

	/* Find the file extension */
	int len = strlen(path), i;
	for (i = len - 1; i >= 0; i--) {
		if (path[i] == '.') {
			break;
		}
	}

	char *mime;
	if (strcmp(path + i, ".html") == 0) { // TODO: it would be nice if these were automatically loaded from a text file
		mime = "text/html";
	} else if (strcmp(path + i, ".jpg") == 0) {
		mime = "image/pjpeg";
	} else if (strcmp(path + i, ".css") == 0) {
		mime = "text/css";
	} else if (strcmp(path + i, ".js") == 0) {
		mime = "text/javascript";
	} else {
		mime = "application/octet-stream";
	}

	char *out = (char *)malloc((strlen(mime) + 1) * sizeof(char)); // TODO: this seems silly... how do you send a string which can only be a few things?
	strcpy(out, mime);
	return out;
}

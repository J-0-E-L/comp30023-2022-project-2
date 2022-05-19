/* Socket setup based on the examples in "Beej's Guide to Network Programming": https://beej.us/guide/bgnet/html/ */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <assert.h>

#include "sockwrap.h"

#define IMPLEMENTS_IPV6
#define MULTITHREADED

#define QUEUE_NUM 10 
#define WORKER_NUM 8
#define BUFFER_LEN 4096
#define TOKEN_LEN 1024

struct context {
	int sockfd;
	char *root;
};

void *serverequest(void *arg);

int getargs(int argc, char **argv, char **protocol, char **port, char **root);
int getlistensock(int *sockfd, char *protocol, char *port);

int validpath(char *path);
char *trap(char *root, char *path);
char *getmime(char *path);

/* For keeping track of threads */
int tcount = 0;
pthread_mutex_t lock;

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

	/* Initialise our mutex */
	if (pthread_mutex_init(&lock, NULL) != 0) {
		return 1;
	}

	while (1) {
		/* Do not accept new connections when we are busy */
		if (tcount >= WORKER_NUM) {
			continue;
		}

		/* Accept incoming connections */
		int newsockfd;
		struct sockaddr_storage addr;
		socklen_t socklen = sizeof(struct sockaddr_storage);
		if ((newsockfd = accept(sockfd, (struct sockaddr *)&addr, &socklen)) == -1) {
			perror("accept");
			return 1;
		}
		
		/* Create a new thread to deal with the connection */
		struct context *cont = (struct context *)malloc(sizeof(struct context));
		assert(cont);
		cont->sockfd = newsockfd;
		cont->root = root;
		pthread_t tid;
		if (pthread_create(&tid, NULL, serverequest, cont) != 0) {
			return 1;
		}
		pthread_mutex_lock(&lock);
		tcount++;
		pthread_mutex_unlock(&lock);
	}

	/* We should never get here */
	return 0;
}

void *serverequest(void *arg) {
	struct context *cont = (struct context *)arg;
	int sockfd = cont->sockfd;
	char *root = cont->root;

	struct sockwrap *sockw = initsockwrap(sockfd, BUFFER_LEN);
	assert(sockw);

	/* Read in the request line */
	char method[TOKEN_LEN + 1], file[TOKEN_LEN + 1], version[TOKEN_LEN + 1];
	char *tokens[3] = {method, file, version}, *dels[3] = {" ", " ", "\r\n"}; // tokens and dels must be the same length
	int tokens_len = 3, valid = 1;

	for (int i = 0; i < tokens_len; i++) {
		int n = recvstr(sockw, tokens[i], TOKEN_LEN, dels[i]);
		if (n == -1) {
			perror("recvstr");
			freesockwrap(sockw);
			close(sockfd);
			pthread_mutex_lock(&lock);
			tcount--;
			pthread_mutex_unlock(&lock);
			return NULL;
		}
		/* Token too large */
		if (n == 2) {
			valid = 0;
			break;
		}
	}

	// TODO: at least look at the headers

	/* Serve the request */
	valid = valid && strcmp(method, "GET") == 0 && validpath(file) && strcmp(version, "HTTP/1.0") == 0;
	if (!valid) {
		sendstr(sockw, "HTTP/1.0 400 Invalid request\r\n\r\n");
	} else {
		char *trapped = trap(root, file);
		assert(trapped);

		FILE *fp = fopen(trapped, "r");
		if (fp == NULL) { // TODO: implement code 403 (read permissions... what level should the program need?)
			sendstr(sockw, "HTTP/1.0 404 File not found\r\n\r\n");
			free(trapped);
		} else {
			sendstr(sockw, "HTTP/1.0 200 OK\r\n");
			sendstr(sockw, "Content-Type: ");
			
			char *mime = getmime(trapped);
			assert(mime);
			free(trapped);
			sendstr(sockw, mime);
			free(mime);
			sendstr(sockw, "\r\n\r\n");

			sendfile(sockw, fp);
			fclose(fp);
		}
	}

	freesockwrap(sockw);
	close(sockfd);
	free(cont);
	pthread_mutex_lock(&lock);
	tcount--;
	pthread_mutex_unlock(&lock);

	return NULL;
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
	if (listen(newsockfd, QUEUE_NUM) == -1) {
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

char *trap(char *root, char *path) {
	int rlen = strlen(root), plen = strlen(path);
	char *out = (char *)malloc((rlen + plen + 1) * sizeof(char));

	strcpy(out, root);
	strcat(out, path);

	enum pattern {BLANK, REG, DOT, DDOT};
	enum pattern pat = BLANK;
	int depth = 0;

	for (int i = rlen, j = rlen; i < rlen + plen + 1; i++) {
		/* Copy out into itself... some ../ 's will be removed in the process */
		out[j] = out[i];
		j++;

		if (out[i] == '/') {
			if (pat == REG) {
				depth++;
			} else if (pat == DDOT) {
				/* Do we escape from the root? */
				if (depth > 0) {
					depth--;
				} else {
					/* Ignore the last ../ */
					j -= 3;
				}
			}
			pat = BLANK;
		/* Update the pattern of the current folder/file */
		} else if (out[i] == '.') {
			if (pat == BLANK) {
				pat = DOT;
			} else if (pat == DOT) {
				pat = DDOT;
			} else if (pat == DDOT) {
				pat = REG;
			}
		} else {
			pat = REG;
		}
	}

	return out;
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
		mime = "image/jpeg";
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

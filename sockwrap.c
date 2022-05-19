#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "sockwrap.h"

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
int recvchar(struct sockwrap *sockw, char *c) {
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
	sockw->start++;
	sockw->n--;
	return 1;
}

/* -1: error, 0: disconnect, 1: string received, 2: string too large */
int recvstr(struct sockwrap *sockw, char *dest, int n, char *del) {

	int buff_len = strlen(del), buff_n = 0;
	char *buff = (char *)malloc(buff_len * sizeof(char));
	if (buff == NULL) {
		return -1;
	}
	
	int str_n = 0;
	while (1) {
		int c = recvchar(sockw, buff + buff_n);
		if (c == -1 || c == 0) {
			return c;
		}
		buff_n++;

		if (buff[buff_n - 1] == del[buff_n - 1]) {
			/* Found the delimeter */
			if (buff_n == buff_len) {
				dest[str_n] = '\0';
				str_n++;
				free(buff);
				return 1;
			}
		} else {
			/* The received string is too long */
			if (str_n + buff_n > n) {
				free(buff);
				return 2;
			}
			/* Move the buffer into the output */
			for (int i = 0; i < buff_n; i++) {
				dest[str_n + i] = buff[i];
			}
			str_n += buff_n;
			buff_n = 0;
		}
	}
	/* We should never get here */
	return -1;
}

/* -1: error, 1: array sent */
int sendarr(struct sockwrap *sockw, char *arr, int len) {
	int start = 0;
	while (start < len) {
		int nsent = send(sockw->sockfd, arr + start, len - start, 0);
		if (nsent == -1) {
			return -1;
		}
		start += nsent;
	}
	return 1;
}

/* -1: error, 1: string sent */
int sendstr(struct sockwrap *sockw, char *src) {
	return sendarr(sockw, src, strlen(src));
}

/* -1: error, 1: file sent */
int sendfile(struct sockwrap *sockw, FILE *fp) {
	char *buff = (char *)malloc(sockw->buff_len * sizeof(char));
	if (buff == NULL) {
		return -1;
	}

	int nread;
	while ((nread = fread(buff, sizeof(char), sockw->buff_len, fp)) > 0) { // TODO: is it dangerous to use a 0 character read to indicate EOF?
		if (sendarr(sockw, buff, nread) == -1) {
			free(buff);
			return -1;
		}
	}
	
	free(buff);
	return 1;
}

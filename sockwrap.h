#ifndef SOCKWRAP_H
#define SOCKWRAP_H

struct sockwrap {
	int sockfd, buff_len, start, n;
	char *buff;
};

struct sockwrap *initsockwrap(int sockfd, int buff_len);
int freesockwrap(struct sockwrap *sockw);

int recvchar(struct sockwrap *sockw, char *c);
int recvstr(struct sockwrap *sockw, char *dest, int n, char *del);

int sendarr(struct sockwrap *sockw, char *arr, int len);
int sendstr(struct sockwrap *sockw, char *src);
int sendfile(struct sockwrap *sockw, FILE *fp);

#endif

// https://stackoverflow.com/questions/11208299/how-to-make-an-http-get-request-in-c-without-libcurl/35680609#35680609
/*
Paul Crocker
Muitas Modificações
*/
// Definir sta linha com 1 ou com 0 se não quiser ver as linhas com debug info.
#define DEBUG 0
#define SCHEDULE_ALG_CONCUR "CONCUR"
#define SCHEDULE_ALG_FIFO "FIFO"

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h> /* getprotobyname */
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "csapp.h"

enum CONSTEXPR
{
    MAX_REQUEST_LEN = 1024
};

unsigned short server_port = 8080; // default port
unsigned short threads_number = 1; // default number of threads
char *hostname = "localhost";
char *schedalg = NULL;
char *file = NULL;
char *file2 = NULL;

unsigned short is_fifo = 0;
int sem_client_count = 0;
sem_t *semaphore_client;

void *httpProtocol(void *args);

int main(int argc, char **argv)
{
    char *fileindex = "/";
    char *filedynamic = "/cgi-bin/adder?150&100";
    char *filestatic = "/godzilla.jpg";

    pthread_t *threads;

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "usage: %s <host> <portnum> <threads> <schedalg> <filename1> <optional: filename2>\n", argv[0]);
        exit(1);
    }

    if (argc > 1)
        hostname = argv[1];
    if (argc > 2)
        server_port = strtoul(argv[2], NULL, 10);
    if (argc > 3)
        threads_number = atoi(argv[3]);
    if (argc > 4)
        schedalg = argv[4];

    if (argc > 5)
        file = argv[5];
    else if (argc > 6)
        file2 = argv[6];
    else
        file = fileindex; // ou escolher outra filedynamic filestatic

    if (strcmp(schedalg, SCHEDULE_ALG_FIFO) == 0)
    {
        printf("FIFO!\n");
        is_fifo = 1;
    }

    // Semaphores Creation
    semaphore_client = (sem_t *)malloc(sizeof(sem_t) * threads_number);

    // Threads Creation
    threads = (pthread_t *)malloc(sizeof(pthread_t) * threads_number);

    while (TRUE)
    {
        // Semaphores Initialization
        if (sem_init(&semaphore_client[0], 0, 1) != 0)
        {
            fprintf(stderr, "Error initializing semaphore\n");
            exit(-1);
        }
        for (int i = 1; i < threads_number; i++)
        {
            if (sem_init(&semaphore_client[i], 0, 0) != 0)
            {
                fprintf(stderr, "Error initializing semaphore\n");
                exit(-1);
            }
        }

        // Create Consumer Threads
        for (int i = 0; i < threads_number; i++)
        {
            int ret = pthread_create(&threads[i], NULL, httpProtocol, (void *)i);
            if (ret)
            {
                fprintf(stderr, "Error creating consumer threads\n");
                exit(-1);
            }
        }

        // Wait for all threads to complete
        for (int i = 0; i < threads_number; i++)
        {
            pthread_join(threads[i], NULL);
        }

        sem_destroy(&semaphore_client[0]);
        for (int i = 1; i < threads_number; i++)
        {
            sem_destroy(&semaphore_client[i]);
        }

        usleep(1000000);
    }

    free(semaphore_client);
    free(threads);

    pthread_exit(NULL);
}

void *httpProtocol(void *args)
{
    int thread_id = (int)args;

    char buffer[BUFSIZ];
    char request[MAX_REQUEST_LEN];
    char request_template[] = "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n";
    struct protoent *protoent;

    in_addr_t in_addr;
    int request_len;
    int socket_file_descriptor;
    ssize_t nbytes;
    struct hostent *hostent;
    struct sockaddr_in sockaddr_in;

    // construção do pedido de http

    if (thread_id % 2 == 0 || file2 == NULL)
    {
        request_len = snprintf(request, MAX_REQUEST_LEN, request_template, file, hostname);
    }
    else
    {
        request_len = snprintf(request, MAX_REQUEST_LEN, request_template, file2, hostname);
    }

    if (request_len >= MAX_REQUEST_LEN)
    {
        fprintf(stderr, "request length large: %d\n", request_len);
        exit(EXIT_FAILURE);
    }

    /* Build the socket. */
    protoent = getprotobyname("tcp");
    if (protoent == NULL)
    {
        perror("getprotobyname");
        exit(EXIT_FAILURE);
    }

    // Open the socket
    socket_file_descriptor = Socket(AF_INET, SOCK_STREAM, protoent->p_proto);

    /* Build the address. */
    // 1 get the hostname address
    hostent = Gethostbyname(hostname);

    in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
    if (in_addr == (in_addr_t)-1)
    {
        fprintf(stderr, "error: inet_addr(\"%s\")\n", *(hostent->h_addr_list));
        exit(EXIT_FAILURE);
    }
    sockaddr_in.sin_addr.s_addr = in_addr;
    sockaddr_in.sin_family = AF_INET;
    sockaddr_in.sin_port = htons(server_port);

    /* Ligar ao servidor */
    Connect(socket_file_descriptor, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in));

    /* Send HTTP request. */
    // FIFO
    if (is_fifo)
    {
        sem_wait(&semaphore_client[thread_id]);
        printf("STATISTIC1 : THREAD_ID: %d\n", thread_id);
        Rio_writen(socket_file_descriptor, request, request_len);
        if (thread_id + 1 < threads_number)
        {
            sem_post(&semaphore_client[thread_id + 1]);
        }
    }
    else
    {
        printf("STATISTIC1 : THREAD_ID: %d\n", thread_id);
        Rio_writen(socket_file_descriptor, request, request_len);
    }

    /* Read the response. */
    if (DEBUG)
        fprintf(stderr, "debug: before first read\n");

    rio_t rio;
    char buf[MAXLINE];

    /* Leituras das linhas da resposta . Os cabecalhos - Headers */
    const int numeroDeHeaders = 5;
    Rio_readinitb(&rio, socket_file_descriptor);
    for (int k = 0; k < numeroDeHeaders; k++)
    {
        Rio_readlineb(&rio, buf, MAXLINE);

        // Envio das estatisticas para o canal de standard error
        if (strstr(buf, "Stat") != NULL)
            fprintf(stderr, "STATISTIC1 : %s", buf);
    }

    // Ler o resto da resposta - o corpo de resposta.
    // Vamos ler em blocos caso que seja uma resposta grande.
    while ((nbytes = Rio_readn(socket_file_descriptor, buffer, BUFSIZ)) > 0)
    {
        if (DEBUG)
            fprintf(stderr, "debug: after a block read\n");
        // commentar a lina seguinte se não quiser ver o output
        Rio_writen(STDOUT_FILENO, buffer, nbytes);
    }

    if (DEBUG)
        fprintf(stderr, "debug: after last read\n");

    Close(socket_file_descriptor);

    pthread_exit(NULL);
}
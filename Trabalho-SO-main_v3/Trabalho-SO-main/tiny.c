/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.

 Fom csapp
 Modified by Paul

 */
#include "csapp.h"

typedef struct
{
  int req_number;
  short int is_static;
  int connfd;
} request_policy_t;

short int policy_is_fifo = 0;
int req_producer_index, req_consumer_index;
int buffer_size, req_fifo_counter;
request_policy_t *request_buffer;

pthread_mutex_t mutex_buffer;
pthread_mutex_t mutex_fifo;
pthread_cond_t cond_producer;
pthread_cond_t cond_consumer;

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void *request_consumer(void *args);
short int is_static(int fd);

int numeroRequestStat = 0;

int main(int argc, char **argv)
{
  int listenfd, connfd, port, threads_number, request_counter;
  unsigned int clientlen; // change to unsigned as sizeof returns unsigned
  char *policy;
  struct sockaddr_in clientaddr;
  pthread_t *threads;

  /* Check command line args */
  if (argc != 5)
  {
    fprintf(stderr, "usage: %s <port> <threads> <buffers> <schedalg>\n", argv[0]);
    exit(1);
  }
  port = atoi(argv[1]);
  threads_number = atoi(argv[2]);
  buffer_size = atoi(argv[3]);
  policy = argv[4];

  if (port < MIN_PORT_NUMBER)
  {
    fprintf(stderr, "The port number must be greater than %d\n", port);
    exit(1);
  }

  printf("port: %d\n", port);
  printf("threads: %d\n", threads_number);
  printf("buffers: %d\n", buffer_size);
  printf("policy: %s\n", policy);

  // ===== RESOURCES ALLOCATION =====
  // POLICY
  policy_is_fifo = strcmp(policy, "FIFO") == 0;
  request_counter = 0;
  req_fifo_counter = 0;
  // Buffer
  req_producer_index = 0;
  req_consumer_index = 0;
  request_buffer = (request_policy_t *)malloc(sizeof(request_policy_t) * buffer_size);

  // Mutex
  if (pthread_mutex_init(&mutex_buffer, NULL) != 0)
  {
    fprintf(stderr, "Error initializing mutex\n");
    exit(-1);
  }
  if (pthread_mutex_init(&mutex_fifo, NULL) != 0)
  {
    fprintf(stderr, "Error initializing mutex\n");
    exit(-1);
  }

  // Conditional Variables
  if (pthread_cond_init(&cond_producer, NULL) != 0)
  {
    fprintf(stderr, "Error initializing conditional variable cond_producer\n");
    exit(-1);
  }
  if (pthread_cond_init(&cond_consumer, NULL) != 0)
  {
    fprintf(stderr, "Error initializing conditional variable cond_consumer\n");
    exit(-1);
  }

  // Threads
  threads = (pthread_t *)malloc(sizeof(pthread_t) * threads_number);

  // ===== SERVER INIT =====
  fprintf(stderr, "Server : %s Running on  <%d>\n", argv[0], port);
  listenfd = Open_listenfd(port);

  // Threads Creation
  // Create Consumer Threads
  for (int i = 0; i < threads_number; i++)
  {
    int ret = pthread_create(&threads[i], NULL, request_consumer, NULL);
    if (ret)
    {
      fprintf(stderr, "Error creating consumer threads\n");
      exit(-1);
    }
  }

  // Accept new http connections and placing the descriptor in the buffer
  clientlen = sizeof(clientaddr);
  while (TRUE)
  {
    request_policy_t req;

    req.req_number = request_counter;
    req.connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    // req.is_static = 1; // Implementar o codigo para verificar se é static ou dynamic

    request_counter++;

    // Escrever para o Buffer Circular de Tamnha N indicado por Buffer_Size
    pthread_mutex_lock(&mutex_buffer);
    // Seccao critica
    while ((req_producer_index + 1) % buffer_size == req_consumer_index)
    {
      pthread_cond_wait(&cond_consumer, &mutex_buffer);
    }
    request_buffer[req_producer_index] = req;
    req_producer_index = ++req_producer_index % buffer_size;
    pthread_cond_signal(&cond_producer);
    pthread_mutex_unlock(&mutex_buffer);
  }

  // Wait for all threads to complete
  /*
  for (int i = 0; i < threads_number; i++)
  {
    pthread_join(threads[i], NULL);
  }
  */

  // Resources Deallocation & Finalization
  pthread_mutex_destroy(&mutex_buffer);
  pthread_mutex_destroy(&mutex_fifo);
  pthread_cond_destroy(&cond_producer);
  pthread_cond_destroy(&cond_consumer);
  free(request_buffer);
  free(threads);

  pthread_exit(NULL);
}
/* $end tinymain */

void *request_consumer(void *args)
{
  request_policy_t req;

  /* Detach self. */
  pthread_detach(pthread_self());

  while (TRUE)
  {
    pthread_mutex_lock(&mutex_buffer);
    while (req_producer_index == req_consumer_index)
    {
      pthread_cond_wait(&cond_producer, &mutex_buffer);
    }
    // Seccao critica
    // Leer do Buffer Circular de Tamnha N indicado por Buffer_Size
    req = request_buffer[req_consumer_index];
    req_consumer_index = ++req_consumer_index % buffer_size;

    pthread_mutex_unlock(&mutex_buffer);
    pthread_cond_signal(&cond_consumer);

    // Checks the FIFO POLICY
    if (policy_is_fifo)
    {
      while (TRUE)
      {
        pthread_mutex_lock(&mutex_fifo);
        if (req.req_number == req_fifo_counter)
        {
          printf("STATISTIC - REQUEST NUMBER %d\n", req.req_number);
          doit(req.connfd);
          Close(req.connfd);
          req_fifo_counter++;
          pthread_mutex_unlock(&mutex_fifo);
          break;
        }
        pthread_mutex_unlock(&mutex_fifo);
      }
    }
    else
    {
      printf("STATISTIC - REQUEST NUMBER: %d\n", req.req_number);
      doit(req.connfd);
      Close(req.connfd);
    }
  }

  pthread_exit(NULL);
}

// is_static
short int is_static(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);             // line:netp:doit:readrequest
  sscanf(buf, "%s %s %s", method, uri, version); // line:netp:doit:parserequest

  /* Parse URI from GET request */
  return parse_uri(uri, filename, cgiargs); // line:netp:doit:staticcheck
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);             // line:netp:doit:readrequest
  sscanf(buf, "%s %s %s", method, uri, version); // line:netp:doit:parserequest
  if (strcasecmp(method, "GET"))
  { // line:netp:doit:beginrequesterr
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }                       // line:netp:doit:endrequesterr
  read_requesthdrs(&rio); // line:netp:doit:readrequesthdrs

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs); // line:netp:doit:staticcheck
  if (stat(filename, &sbuf) < 0)
  { // line:netp:doit:beginnotfound
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  } // line:netp:doit:endnotfound

  if (is_static)
  { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    { // line:netp:doit:readable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // printf("FD %d\n", fd);
    // printf("filename %s\n", filename);
    // printf("buf_size %d\n", sbuf.st_size);
    serve_static(fd, filename, sbuf.st_size); // line:netp:doit:servestatic
  }
  else
  { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    { // line:netp:doit:executable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // line:netp:doit:servedynamic
  }
}

/* $end doit */

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  { // line:netp:readhdrs:checkterm
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  { /* Static content */             // line:netp:parseuri:isstatic
    strcpy(cgiargs, "");             // line:netp:parseuri:clearcgi
    strcpy(filename, ".");           // line:netp:parseuri:beginconvert1
    strcat(filename, uri);           // line:netp:parseuri:endconvert1
    if (uri[strlen(uri) - 1] == '/') // line:netp:parseuri:slashcheck
      strcat(filename, "home.html"); // line:netp:parseuri:appenddefault
    return 1;
  }
  else
  { /* Dynamic content */  // line:netp:parseuri:isdynamic
    ptr = index(uri, '?'); // line:netp:parseuri:beginextract
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, ""); // line:netp:parseuri:endextract
    strcpy(filename, "."); // line:netp:parseuri:beginconvert2
    strcat(filename, uri); // line:netp:parseuri:endconvert2
    return 0;
  }
}

/* $end parse_uri */

/*
 * serve_static - copy a file back to the client
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);    // line:netp:servestatic:getfiletype
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // line:netp:servestatic:beginserve
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sRequestStat: %d\r\n", buf, numeroRequestStat++);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // line:netp:servestatic:endserve

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);                        // line:netp:servestatic:open
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // line:netp:servestatic:mmap
  Close(srcfd);                                               // line:netp:servestatic:close

  // printf("HEADER\n%s\n", buf);
  // printf("RESPONSE\n%s\n", srcp);

  Rio_writen(fd, srcp, filesize); // line:netp:servestatic:write
  Munmap(srcp, filesize);         // line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 * Deverá adicionar mais tipos
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  int pipefd[2];

  /*Paul Crocker
    Changed so that client content is piped back to parent
  */
  Pipe(pipefd);

  if (Fork() == 0)
  { /* child */ // line:netp:servedynamic:fork
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); // line:netp:servedynamic:setenv
    // Dup2 (fd, STDOUT_FILENO);	/* Redirect stdout to client *///line:netp:servedynamic:dup2
    Dup2(pipefd[1], STDOUT_FILENO);

    Execve(filename, emptylist, environ); /* Run CGI program */ // line:netp:servedynamic:execve
  }
  close(pipefd[1]);
  char content[1024]; // max size that cgi program will return

  int contentLength = read(pipefd[0], content, 1024);
  Wait(NULL); /* Parent waits for and reaps child */ // line:netp:servedynamic:wait

  /* Generate the HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // line:netp:servestatic:beginserve
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sRequestStat: %d\r\n", buf, numeroRequestStat++);
  sprintf(buf, "%sContent-length: %d\r\n", buf, contentLength);
  sprintf(buf, "%sContent-type: text/html\r\n\r\n", buf);
  Rio_writen(fd, buf, strlen(buf)); // line:netp:servestatic:endserve

  Rio_writen(fd, content, contentLength);
}

/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  /* Fazer primeiro visto que precisamos de saber o tamanho do body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sRequestStat: %d\r\n", buf, numeroRequestStat++);
  Rio_writen(fd, buf, strlen(buf));

  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  Rio_writen(fd, body, strlen(body));
}

/* $end clienterror */

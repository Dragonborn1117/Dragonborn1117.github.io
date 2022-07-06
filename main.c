#include "wrapper.h"


#define M_GET 1
#define M_POST 2
#define M_HEAD 3
#define M_NONE 0

const int MAXThreads = 4;
const int buffersize = 1000;



void doit(int fd);
void read_requesthdrs(rio_t *rp, char *post_content, int mtd);
int parse_uri(char *url, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filetype, int mtd);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, int mtd);
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg, int mtd);



int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_in clientaddr;


    //检查输入合法性
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = open_listenfd(argv[1]);


    while (1)
    {
        clientlen = sizeof(port);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        if(fork() == 0) {
			//基于进程的并发处理  共享文件表，但不共享地址空间，父子进程独立的地址空间不便于共享信息
			//父子进程的已连接描述符指向同一文件表表项，父进程不关闭的话，将导致该connfd永远不会关闭，发生内存泄露
			//
			close(listenfd); //子进程关闭监听描述符  
			doit(connfd);
			close(connfd);	//子进程关闭已连接描述符
			exit(0);        //退出子进程（重要）
		}
		close(connfd);		//父进程关闭已连接描述符
    }

    return 0;
}



//处理http事务
//支持GET POST HEAD
void doit(int fd)
{
    int is_static, mtd;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    
    char post_content[MAXLINE];

    rio_t rio;

    //读取请求和header
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);

    if (!strcmp(method, "GET")) mtd = M_GET;
    else if (!strcmp(method, "POST")) mtd = M_POST;
    else if (!strcmp(method, "HEAD")) mtd = M_HEAD;
    else mtd = M_NONE;
    
    if (mtd == M_NONE||mtd == M_HEAD)
    {
        clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method", mtd);
        return;
    }
    read_requesthdrs(&rio, post_content, mtd);

    //解析请求
    is_static = parse_uri(url, filename, cgiargs);
    if (stat(filename, &sbuf) < 0)
    {
        clienterror(fd, filename, "404", "Not found",
                "coundn't find this file", mtd);
        return;
    }

    if (is_static)
    {
        if (mtd == M_POST)
        {
            clienterror(fd, filename, "405", "Method Not Allowed",
                    "Request method POST is not allowed for the url", mtd);
            return;
        }
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                    "couldn't read the file", mtd);
            return;
        }
        serve_static(fd, filename, sbuf.st_size, mtd);
    }
    else
    {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                    "couldn't run the GET program", mtd);
            return;
        }
        if (mtd == M_POST) strcpy(cgiargs, post_content);
        serve_dynamic(fd, filename, cgiargs, mtd);
    }
}

//将错误信息返回给客户端
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg, int mtd)
{
    char buf[MAXLINE], body[MAXBUF];

    //设置错误页面的body
    sprintf(body, "<HTML><TITLE>Web Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>Web server</em>\r\n", body);

    //输出错误页面
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    if (mtd != M_HEAD)
    {
        rio_writen(fd, buf, strlen(buf));
        rio_writen(fd, body, strlen(body));
    }
}

//get则忽略报文post读取
void read_requesthdrs(rio_t *rp, char *post_content, int mtd)
{
    char buf[MAXLINE];
    int contentlen = 0;

    rio_readlineb(rp, buf, MAXLINE);
    if (mtd == M_POST && strstr(buf, "Content-Length: ") == buf)
        contentlen = atoi(buf + strlen("Content-Length: "));
    while (strcmp(buf, "\r\n"))
    {
        rio_readlineb(rp, buf, MAXLINE);
        printf("info: %s", buf);

        //取得post的参数长度
        if (mtd == M_POST && strstr(buf, "Content-Length: ") == buf)
            contentlen = atoi(buf + strlen("Content-Length: "));
    }
    if (mtd == M_POST)
    {
        contentlen = rio_readnb(rp, post_content, contentlen);
        post_content[contentlen] = '\0';
        printf("POST_CONTENT: %s\n", post_content);
    }
    return;
}

//判断请求内容
int parse_uri(char *url, char *filename, char *cgiargs)
{
    char *ptr;

    //static server
    if (!strstr(url, "cgi-bin"))
    {
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, url);
        if (url[strlen(url)-1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    else  //dynamic server
    {
        ptr = index(url, '?');
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
        {
            strcpy(cgiargs, "");
        }
        strcpy(filename, ".");
        strcat(filename, url);
        return 0;
    }
}

//静态服务
//可以使用HTML、纯文本文件、GIF、JPG格式到文件
void serve_static(int fd, char *filename, int filesize, int mtd)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXLINE];

    //给客户端发送响应报头
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    rio_writen(fd, buf, strlen(buf));

    //给客户端发送响应报文
    srcfd = open(filename, O_RDONLY, 0);
    srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close(srcfd);
    rio_writen(fd, srcp, filesize);
    munmap(srcp, filesize);
}

//从文件名中获取文件类型
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

void serve_dynamic(int fd, char *filename, char *cgiargs, int mtd)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    //返回前一部分报文
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Web Server\r\n");
    rio_writen(fd, buf, strlen(buf));

    if (mtd == M_HEAD) return;

    if (fork() == 0)
    {
        //正式的server会在这里设置所有到CGI环境
        setenv("QUERY_STRING", cgiargs, 1); //这里只设置QUERY_STRING
        dup2(fd, STDOUT_FILENO);
        execve(filename, emptylist, environ);
    }
    wait(NULL);
}

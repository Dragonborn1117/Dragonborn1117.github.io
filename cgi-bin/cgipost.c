#include "../wrapper.h"

int number(char *s)
{
    int i = 0;
    while (s[i] != '=')
        i++;
    return atoi(s + i + 1);
}

int main() 
{
    char *buf, *p;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int n1=0, n2=0;

    //参数检查正确
    if ((buf = getenv("QUERY_STRING")) != NULL)
    {
        p = strchr(buf, '&');
        *p = '\0';
        strcpy(arg1, buf);
        strcpy(arg2, p+1);
        n1 = number(arg1);
        n2 = number(arg2);
    }

    //创建返回的body
    sprintf(content, "Welcome to register.com: \r\n<p>");
    sprintf(content, "%sYour user id is: %d\r\n<p>", content, n1);
    sprintf(content, "%sYour passwd is: %d\r\n<p>", content, n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);
  
    //输出body
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    printf("%s", content);
    fflush(stdout);

    exit(0);
}

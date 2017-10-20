// epollser
#include"unp.h"
#include<sys/socket.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<stdio.h>
#include<fcntl.h>
#include<strings.h>
#include<sys/resource.h>

#define MAX_EVENTS 30000
//#define LISTENQ 20
#define BUFSIZE 512
#define LEN_PAC 40
struct var_pac
{
    int len;
    //int send_offset;//发送偏移量；
    int write_offset;//发送的偏移量
    int data_offset;//读包的数据部分的偏移量
    int fd;
    int flag;//标志可读事件在结构体头部和数据部分的转移
    int send_file_offset;
    int read_offset;//读包的头部偏移量
    int rcv_file_offset;
    int file_len;
    char *buffer;
};

void setnonblock(int sock)
{
    int opts;
    opts = fcntl(sock, F_GETFL);
    opts = opts|O_NONBLOCK;
    fcntl(sock, F_SETFL, opts);
}

int main(int argc, char *argv[])
{
    int epfd, nfds, i, n, listenfd, connfd, sockfd, write_n, len, read_n;
    //FILE *output_file_fd;
    //char rev[LEN_PAC], send[BUFSIZE];
    socklen_t clilen;
    char *rcvbuffer;//*sendbuffer;
    struct epoll_event ev, events[MAX_EVENTS];
    struct sockaddr_in servaddr, cliaddr;
    struct var_pac *write_pac, *read_pac, *ptr_pac, *listenfd_pac; //*temp_pac
    struct rlimit *rlim;

    rlim = (struct rlimit *)malloc(sizeof(struct rlimit));
    rlim->rlim_cur = MAX_EVENTS;
    rlim->rlim_max = MAX_EVENTS;
    if((setrlimit(RLIMIT_NOFILE, rlim)) < 0)
        printf("setrlimit error\n");

    //创建、初始化listenfd
    epfd = epoll_create(MAX_EVENTS);
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    //setnonblock(listenfd);
    listenfd_pac = (struct var_pac *)malloc(sizeof(struct var_pac));
    listenfd_pac->fd = listenfd;
    listenfd_pac->read_offset = 0;
    listenfd_pac->data_offset = 0;
    listenfd_pac->write_offset = 0;
    listenfd_pac->send_file_offset = 0;
    listenfd_pac->len = 0;
    listenfd_pac->flag = 0;
    listenfd_pac->rcv_file_offset = 0;
    listenfd_pac->file_len = 0;
    listenfd_pac->buffer = NULL;
    ev.data.ptr = listenfd_pac;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
    listen(listenfd, LISTENQ);
    
    for(; ;)
    {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, 500);
        for(i = 0; i < nfds; ++i)
        {
            read_pac = events[i].data.ptr;
            sockfd = read_pac->fd;
            if(sockfd == listenfd)//new connection
            {
                //printf("new connection\n");
                //建立连接
                connfd = accept(sockfd, (SA *)&cliaddr, &clilen);
                //printf("new connection\n");
                setnonblock(connfd);
                //printf("connfd %d\n", connfd);
                //初始化
                ptr_pac = (struct var_pac *)malloc(sizeof(struct var_pac));
                ptr_pac->fd = connfd;
                ptr_pac->len = 0;
                ptr_pac->read_offset = 0;
                ptr_pac->write_offset = 0;
                ptr_pac->send_file_offset = 0;
                ptr_pac->data_offset = 0;
                ptr_pac->flag = 0;
                ptr_pac->rcv_file_offset = 0;
                ptr_pac->file_len = 0;
                ptr_pac->buffer = NULL;
                //注册
                ev.data.ptr = ptr_pac;
                ev.events = EPOLLIN;
                epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
            }
            else if(events[i].events & EPOLLIN)//可读事件
            {
                if((read_pac->flag == 0) && (read_pac->read_offset < LEN_PAC))//读包头
                {
                    //printf("test read_pac->flag = 0 \n");
                    n = read(sockfd, read_pac + read_pac->read_offset, LEN_PAC);
                    //printf("sockfd %d n %d\n", sockfd, n);
                    if(n < 0)
                    {
                        if(errno == ECONNRESET)
                        {
                            //printf("read pac error %d\n", errno);
                            close(sockfd);
                            ev.data.ptr = read_pac;
                            epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                        }
                        if(errno == EAGAIN)
                        {
                            printf("sockfd %d line 144 read error %s\n",sockfd, strerror(errno));
                            ev.data.ptr = read_pac;
                            //ev.events = EPOLLIN | EPOLLET;
                            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                    }
                    else if(n == 0)
                    {
                        close(sockfd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                        free(read_pac);
                    }
                    else if((n + read_pac->read_offset) == LEN_PAC)
                    {
                        //printf("sockfd %d send_file_offset %d\n", sockfd, read_pac->send_file_offset);
                        read_pac->flag = 1;
                        read_pac->read_offset = 0;
                        ev.data.ptr = read_pac;
                        //ev.events = EPOLLIN | EPOLLET;
                        //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                    }
                    else if((n + read_pac->read_offset)< LEN_PAC )
                    {
                        read_pac->read_offset += n;
                        read_pac->flag = 0;
                        ev.data.ptr = read_pac;
                        //ev.events = EPOLLIN | EPOLLET;
                        //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                    }
                }
                else//flag == 1 包头足够，开始读数据
                {
                    if(read_pac->flag == 1)
                    {
                        //printf("test flag = 1\n");
                        len = read_pac->len;
                        //printf("sockfd %d data_pac -> len = %d\n", sockfd, read_pac->len);//测试len
                        //printf("sockfd %d data_offset %d\n", sockfd, read_pac->data_offset);
                        if(read_pac->data_offset < len)
                        {
                            if(read_pac->data_offset == 0)
                            {
                                if((rcvbuffer = (char *)malloc(sizeof(char)*len)) == NULL)
                                    printf("rcvbuffer malloc error\n");
                                read_pac->buffer = rcvbuffer;
                            }
                            read_n = read(sockfd, read_pac->buffer + read_pac->data_offset, len - read_pac->data_offset);
                            //printf("sockfd %d read_n %d\n", sockfd, read_n);
                            if(read_n < 0)
                            {
                                if(errno == EAGAIN)
                                {
                                    printf("EAGAIN\n");
                                    ev.data.ptr = read_pac;
                                    ev.events = EPOLLIN | EPOLLET;
                                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                                else
                                {
                                    read_pac->flag = 1;
                                    read_pac->fd = sockfd;
                                    read_pac->data_offset = 0;
                                    ev.data.ptr = read_pac;
                                    //ev.events = EPOLLIN | EPOLLET;
                                    //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                            }
                            else if(read_n == 0)
                            {
                                //printf("sockfd %d read_n = 0\n", sockfd);
                                close(sockfd);
                                ev.data.ptr = read_pac;
                                epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                            }
                            else if((read_n + read_pac->data_offset) < len)//此次未读完包的数据
                            {
                                read_pac->flag = 1;
                                read_pac->data_offset += read_n;
                                //printf("-------sockfd %d data_offset %d----------\n", sockfd, read_pac->data_offset);
                                ev.data.ptr = read_pac;
                                //ev.events = EPOLLIN | EPOLLET;
                                //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                            }
                            if((read_n + read_pac->data_offset) == len)//注册可写事件
                            {
                                //printf("sockfd %d 一次数据接收完成\n", sockfd);
                                read_pac->flag = 0;
                                read_pac->data_offset = 0;
                                read_pac->write_offset = 0;
                                ev.data.ptr = read_pac;
                                ev.events = EPOLLOUT;
                                epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                            }
                        }   
                    }
                }    
            }
            else if(events[i].events&EPOLLOUT)//可写
            {
                write_pac = events[i].data.ptr;
                len = write_pac->len;
                sockfd = write_pac->fd;
                if(write_pac->write_offset < (len + LEN_PAC))
                {
                    if(write_pac->write_offset == 0)//第一次写
                    {
                        //printf("第一次写\n");
                        write_n = write(sockfd, write_pac, len + LEN_PAC);
                        if(write_n < (len + LEN_PAC))
                        {
                            write_pac->write_offset += write_n;
                            ev.data.ptr = write_pac;
                            //ev.events = EPOLLOUT | EPOLLET;
                            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                        if(write_n == (len + LEN_PAC))//写完，注册可读事件
                        {
                            //printf("sockfd %d 写完一次数据\n", sockfd);
                            write_pac->write_offset = 0;
                            free(write_pac->buffer);
                            ev.data.ptr = write_pac;
                            ev.events = EPOLLIN;
                            epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                    }
                    else
                    {
                        write_n =  write(sockfd, write_pac + write_pac->write_offset, (len + LEN_PAC)-write_pac->write_offset); 
                        if(write_n < 0)
                            printf("write error\n");
                        else if((write_n + write_pac->write_offset) < (len + LEN_PAC))
                        {
                            write_pac->write_offset += write_n;
                            ev.data.ptr = write_pac;
                            //ev.events = EPOLLOUT | EPOLLET;
                            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                        else if((write_n + write_pac->write_offset) == (len + LEN_PAC))//注册可读事件
                        {
                            //printf("sockfd %d 写完一次数据\n", sockfd);
                            write_pac->write_offset = 0;
                            free(write_pac->buffer);
                            ev.data.ptr = write_pac;
                            ev.events = EPOLLIN;
                            epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                    }
                }
            }
            else if((events[i].events & EPOLLERR))
            {
                read_pac = events[i].data.ptr;
                sockfd = read_pac->fd;
                close(sockfd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
            }
        }
    }
}

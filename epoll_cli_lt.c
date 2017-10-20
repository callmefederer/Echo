//tcpcli
#include<sys/resource.h>
#include"unp.h"
#include<sys/socket.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<stdio.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<error.c>

#define MAX_EVENTS 30000
#define LEN_PAC 40
#define BUFSIZE 512

struct var_pac
{
    int len;
    int write_offset;
    int data_offset;
    int fd;
    int flag;
    int send_file_offset;//文件写偏移量
    int read_offset;
    int rcv_file_offset;
    int file_len;
    char *buffer;
};//成员变量含义参考服务器代码部分

void setnonblock(int sock)
{
    int opts;
    opts = fcntl(sock, F_GETFL);
    opts = opts|O_NONBLOCK;
    fcntl(sock, F_SETFL, opts);
}

int main(int argc, char **argv)
{
    if(argc != 4)
        printf("<IP><NUM_SOCKETS><FILE>\n");

    int ret, arg_int, len, status, file_len;
    struct stat *stat;

    arg_int = atoi(argv[2]);
    //printf("arg_int %d\n", arg_int);
    static int write_done_count = 0;
    static int write_count = 0;
    static int read_count = 0;
    static int read_done_count = 0;
    time_t time_start, time_end;
    int epfd, nfds, i, sockfd, fd, max_events, file_len_rcv;
    char *file;
    struct epoll_event ev, events[arg_int];
    struct sockaddr_in servaddr;
    struct var_pac  *mypac, *read_pac, *write_pac;// *data_pac, *data_tmp_pac, *head;
    struct rlimit *rlim_new;
    char *rcvbuffer, *sendbuffer;

    //设置rlimt
    if(arg_int > 1024)
    {
        rlim_new = (struct rlimit *)malloc(sizeof(struct rlimit));
        rlim_new->rlim_cur = arg_int;
        rlim_new->rlim_max = arg_int;
        if((setrlimit(RLIMIT_NOFILE, rlim_new)) < 0)
            err_sys("setrlimit error\n");
    }

    if((fd = open(argv[3], O_RDONLY)) < 0)
        err_sys("open error\n");
    stat = (struct stat *)malloc(sizeof(struct stat));
    if((status = fstat(fd, stat)) < 0)
        err_sys("fstat eror\n");
    else
    {
        file_len = stat->st_size;
    }
    file = (char *)malloc(file_len*sizeof(char));
    read(fd, file, file_len);//读入内存


    max_events = arg_int;

    epfd = epoll_create(max_events);
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    for(i = 0; i < max_events; i++)
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        connect(sockfd, (SA *) &servaddr, sizeof(servaddr));
        setnonblock(sockfd);
        if((mypac = (struct var_pac *)malloc(sizeof(struct var_pac))) == NULL)
            err_sys("mypa malloc error\n");
        //printf("sockfd %d\n", sockfd);
        mypac->fd = sockfd;
        //printf("mypac fd %d \n", mypac->fd);
        mypac->len = 0;
        mypac->write_offset = 0;
        mypac->data_offset = 0;
        mypac->read_offset = 0;
        mypac->send_file_offset = 0;
        mypac->flag = 0;
        mypac->rcv_file_offset = 0;
        mypac->file_len = file_len;
        mypac->buffer = NULL;
        ev.data.ptr = mypac;
        //mypac = ev.data.ptr;
        //printf("mypac %d\n", mypac);
        ev.events = EPOLLOUT;//坑
        epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
    }
    //创建连接
    /*
    for(i = 0; i < max_events; i++)
    {
        events[i].data.ptr = NULL;
    }//initialize
    */
    //bzero(send, sizeof(send));
    //bzero(rev, sizeof(rev));
    //printf("--------------------\n");
    if(arg_int > 1024)
        arg_int -= 5;
    time(&time_start);
    for(; ;)
    {
        nfds = epoll_wait(epfd, events, max_events, 500);
        //printf("nfds %d\n", nfds);
        for(i = 0; i < nfds; i++)
        {
            if(events[i].events&EPOLLIN)
            {
                read_pac = events[i].data.ptr;//工作指针
                sockfd = read_pac->fd;
                file_len_rcv = read_pac->send_file_offset;
                //printf("file_len_rcv %d\n", file_len_rcv);
                if((read_pac->flag == 0) && (read_pac->read_offset < LEN_PAC))//读包头
                {
                    //printf("flag = 0 && read_offset < LEN_PAC\n");
                    ret = read(sockfd, read_pac + read_pac->read_offset, LEN_PAC - read_pac->read_offset);
                    if(ret < 0)
                    {
                        if(errno == ECONNRESET)
                        {
                            close(sockfd);
                            epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                        }
                        if(errno == EAGAIN)
                        {
                            printf("sockfd %d line 152 read error %s \n", sockfd, strerror(errno));
                            ev.data.ptr = read_pac;
                            ev.events = EPOLLIN;
                            epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                    }
                    else if(ret == 0)
                    {
                        close(fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                    }
                    else if((ret + read_pac->read_offset) < LEN_PAC)//继续读包头
                    {
                        read_pac->read_offset += ret;
                        ev.data.ptr = read_pac;
                        //ev.events = EPOLLIN | EPOLLET;
                        //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                    }
                    else if((ret + read_pac->read_offset) == LEN_PAC)//读完包头
                    {
                        read_pac->flag = 1;
                        read_pac->read_offset = 0;
                        ev.data.ptr = read_pac;
                        //ev.events = EPOLLIN | EPOLLET;
                        //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                    }
                    else
                    {
                        printf("这个判断竟然到这儿了！！！！\n");
                    }
                }
                else//flag = 1
                {
                    len = read_pac->len;
                    //printf("flag = 1 test len = %d\n", len);
                    if(read_pac->data_offset < len)
                    {
                        //printf("test flag = 1\n");
                        if(read_pac->data_offset == 0)
                        {
                            rcvbuffer = (char *)malloc(sizeof(char)*(len-read_pac->data_offset));
                            read_pac->buffer = rcvbuffer;
                        }
                        ret = read(sockfd, read_pac->buffer + read_pac->data_offset, len - read_pac->data_offset);
                        //printf("flag = 1 ret %d\n", ret);
                        if(ret < 0)
                        {
                            if(errno == ECONNRESET)
                            {
                                close(sockfd);
                                epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                            }
                            else if(errno == EAGAIN)
                            {
                                printf("sockfd %d line 205 read error %s \n", sockfd, strerror(errno));
                                ev.data.ptr = read_pac;
                                ev.events = EPOLLIN | EPOLLET;
                                epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                            }
                            else
                            {
                                printf("read error %s\n", strerror(errno));
                            }
                        }
                        else if(ret == 0)
                        {
                            close(fd);
                            epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                        }
                        else if((ret + read_pac->data_offset) < len)
                        {
                            read_pac->data_offset += ret;
                            ev.data.ptr = read_pac;
                            //ev.events = EPOLLIN | EPOLLET;
                            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                        }
                        else if((ret + read_pac->data_offset) == len)//读完len长度的数据，注册可写事件
                        {
                            //printf("sockfd %d 读完一次数据\n", sockfd);
                            read_count++;
                            read_pac->rcv_file_offset += len;
                            free(read_pac->buffer);
                            //printf("sockfd %d rcv_file_offset %d\n", sockfd, read_pac->rcv_file_offset);
                            if(read_pac->rcv_file_offset == file_len)
                            {
                                //printf("sockfd %d 文件接收完成\n", sockfd);
                                read_done_count++;
                                close(sockfd);   
                                ev.data.ptr = read_pac;
                                epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                                free(read_pac);
                            }
                            else
                            {
                                read_pac->fd = sockfd;
                                read_pac->data_offset = 0;
                                read_pac->send_file_offset = file_len_rcv;
                                read_pac->flag = 0;
                                read_pac->len = 0;
                                ev.data.ptr = read_pac;
                                ev.events = EPOLLOUT;
                                epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                            }
                        }
                    }
                }
            }
            else if(events[i].events&EPOLLOUT)
            {
                write_pac = events[i].data.ptr;
                sockfd = write_pac->fd;
                //printf("send_file_offset %d\n", write_pac->send_file_offset); 
                if(write_pac->send_file_offset < file_len)//有没有逻辑错误
                {
                    if(write_pac->len == 0)
                    {
                        if((file_len - write_pac->send_file_offset) > BUFSIZE)
                        {
                            if((sendbuffer = (char *)malloc(sizeof(char)*BUFSIZE)))
                                write_pac->buffer = sendbuffer;
                            else
                                err_sys("buffer malloc error\n");
                        }
                        else
                        {
                            if((sendbuffer = (char *)malloc(sizeof(char)*(file_len-write_pac->send_file_offset))))
                                write_pac->buffer = sendbuffer;
                            else
                                err_sys("buffer malloc error\n");
                        }
                        if(memcpy(write_pac->buffer, file+write_pac->send_file_offset ,((file_len-write_pac->send_file_offset)>BUFSIZE)?BUFSIZE:(file_len-write_pac->send_file_offset)))//包数据
                        {
                            len = (file_len-write_pac->send_file_offset) > BUFSIZE ? BUFSIZE : (file_len - write_pac->send_file_offset);//数据长度
                            //printf("test2 len %d\n", len);
                            write_pac->send_file_offset += len;
                            write_pac->len = len;//记录读取的包数据长度
                            if((ret = write(sockfd, write_pac, len + LEN_PAC)) < 0)
                            {
                                if(errno == ECONNRESET)
                                   {
                                       close(sockfd);
                                       epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                                   }
                                else
                                {
                                    printf("write error %d line: 297 \n", errno);
                                    ev.data.ptr = write_pac;
                                    //ev.events = EPOLLOUT | EPOLLET;
                                    //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                            }
                            else if(ret < (len + LEN_PAC))
                            {
                                write_pac->write_offset += ret;
                                ev.data.ptr = write_pac;
                                //ev.events = EPOLLOUT | EPOLLET;
                                //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                            }
                            else if(ret == (len + LEN_PAC))//写完数据
                            {
                                write_count++;
                                free(write_pac->buffer);
                                if(write_pac->send_file_offset == file_len)//写完文件数据，关闭套接字
                                {
                                    //printf("sockfd %d 文件传送完成\n", sockfd);
                                    write_done_count++;
                                    write_pac->write_offset = 0;
                                    ev.data.ptr = write_pac;
                                    ev.events = EPOLLIN;
                                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                                else
                                {
                                    //printf("sockfd %d 写完一次数据\n", sockfd);
                                    write_pac->fd = sockfd;
                                    write_pac->len = 0;
                                    write_pac->write_offset = 0;
                                    ev.data.ptr = write_pac;
                                    ev.events = EPOLLIN;
                                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                            }
                        }
                        else
                        {
                            printf("memcpy error\n");
                        }
                    }
                    else//write_pac->len != 0
                    {
                        if(write_pac->write_offset < (write_pac->len))
                        {
                            ret = write(sockfd, write_pac + write_pac->write_offset,(len + LEN_PAC) - write_pac->write_offset);
                            if((ret + write_pac->write_offset) < (len + LEN_PAC))
                            {
                                write_pac->write_offset += ret;
                                ev.data.ptr = write_pac;
                                //ev.events = EPOLLOUT | EPOLLET;
                                //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);                
                            }
                            else if((ret + write_pac->write_offset) == (len + LEN_PAC))
                            {
                                write_count++;
                                if(write_pac->send_file_offset == file_len)//写完文件数据，关闭套接字
                                {
                                    //printf("sockfd %d 文件传输完成\n", sockfd);
                                    //close(sockfd);
                                    write_done_count++;
                                    free(write_pac->buffer);
                                    ev.data.ptr = write_pac;
                                    ev.events = EPOLLIN;
                                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                                else
                                {
                                    //printf("sockfd %d 写完一次数据\n", sockfd);
                                    //write_pac->send_file_offset += len;
                                    write_pac->fd = sockfd;
                                    write_pac->len = 0;
                                    write_pac->write_offset = 0;
                                    free(write_pac->buffer);
                                    ev.data.ptr = write_pac;
                                    ev.events = EPOLLIN;
                                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                                }
                            }
                        }
                    }
                }
            }
        }
        //printf("write_done_count %d read_done_count %d\n", write_done_count, read_done_count);
        if((read_done_count == arg_int)&&(read_done_count == write_done_count))
        {
            time(&time_end);
            printf("time %ld\n", time_end - time_start);
            printf("write_count %d\n", write_count + read_count);
            exit(1);
        }
    }
}

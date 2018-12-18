/************************************************/
/*    	HttpClient	                            */
/*    	Author : hefeiping	                    */
/*    	Date   : 2017-04-25	                    */
/*    	Update : 2017-04-25	                    */
/*    	Version: ST                             */
/************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define Eprintf   printf("[%s][%d] ",__FILE__,__LINE__);printf

#define HTTP_BUFFER_LEN   100*1024

#define RET_REREAD_SOCKET -100
#define RET_TIME_OUT      -200

#define ONCE_READ_LEN        10240  /* 一次从端口中读取http报文长度 */
#define MAX_CHUNKED_NUM      100   /* 支持的最大分片数 */
#define MAX_CHUNKED_HEAD_LEN 10    /* 分片编码中前面数据长度字符串的最大长度
                                    * 数据长度（16进制字符串）+\r\n+数据+\r\n */

/* 字符串 */
typedef struct {
    int   len;
    char *data;
} hfp_str_t;

/* http://www.qianhai.com/index.html */
typedef struct {
    hfp_str_t  protocol;   /* 协议:http */
    hfp_str_t  address;    /* 主机：www.qianhai.com */
    hfp_str_t  port;       /* 端口号: 默认发送80端口 */
    hfp_str_t  page;       /* 路径:/index.html */
} hfp_http_url_t;

typedef struct {
    hfp_str_t        meth;             /* 请求方法 */

    /* 状态行 */
    hfp_str_t         http_version;    /* 协议版本 */
    hfp_str_t         status_code;     /* 响应码 */
    hfp_str_t         reason_phrase;   /* 响应码的文本描述 */

    /* 消息报头 */
    hfp_str_t   location;              /* 重定向地址 */
    hfp_str_t   transfer_encoding;     /* Transfer-Encoding:chunked */
    hfp_str_t   content_length;
    int         content_length_n;      /* 根据Content_Length 计算出的HTTP包体大小 */

    /* 响应正文 */
    int         body_num;
    hfp_str_t   body[MAX_CHUNKED_NUM];

} hfp_http_object_t;

int	 http_alarm_flag;

void http_sigset_alarm( int sig )
{
    http_alarm_flag = -1;
    alarm( 0 );
    sigset( SIGALRM, SIG_DFL ) ;
}

void http_set_alarm( int time_out )
{
    http_alarm_flag = 0;
	sigset ( SIGALRM , http_sigset_alarm );
	alarm( time_out );
}

void http_unset_alarm( void )
{
    alarm( 0 ) ;
    sigset( SIGALRM, SIG_DFL ) ;
}

void http_to_lower( char *str, int len )
{
    int i;
    for( i=0; i<len; i++ )
    {
        str[i] = tolower( str[i] );
    }
}

/* str = "12345"
 * len = 3
 * 返回 123
 */
int http_atoi( char *str, int len)
{
    int i=0, num=0;

    for( i=0; i<len; i++)
        num = num*10 + str[i]-'0';

    return num;
}

/***************************
 * str :"9B" 字母不区分大小写
 * ret  （'B'-'A'+10） + 9 * 16 = 155
        <0 err
 ***************************/
int http_hexstr2int( char *str, int str_len)
{
    int i,m = 1;
    int tmp;
    int result=0;

    for( i=str_len-1; i>=0; i--)
    {
        if( str[i] >= '0' && str[i] <= '9' )
        {
            tmp = str[i] - '0';
        }
        else if( str[i] >= 'A' && str[i] <= 'F' )
        {
            tmp = str[i] - 'A' + 10;
        }
        else if( str[i] >= 'a' && str[i] <= 'f' )
        {
            tmp = str[i] - 'a' + 10;
        }
        else
        {
            return -1;
        }

        result += (tmp*m);

        m *= 16;
    }

    return result;
}

int http_split_url( char *url, hfp_http_url_t *http_url_obj )
{
    char *head, *tail;

    memset( http_url_obj, 0, sizeof( hfp_http_url_t ) );

    head = url;

    /* 协议 */
    if( ( tail = strstr(head, "://" ) ) != NULL )
    {
        /* 案例:http://www.qianhai.com */
        http_url_obj->protocol.data = head;
        http_url_obj->protocol.len  = tail - head;
        head = tail + 3;
    }

    /* 主机和端口*/
    if( ( tail = strchr( head, ':' ) ) == NULL )
    {
        if( ( tail = strchr( head, '/' ) ) == NULL )
        {
            /* 案例:www.qianhai.com */
            http_url_obj->address.data = head;
            http_url_obj->address.len  = strlen(head);
            return 0;
        }

        /* 案例:www.qianhai.com/doPay */
        http_url_obj->address.data = head;
        http_url_obj->address.len  = tail-head;
    }
    else
    {
        http_url_obj->address.data = head;
        http_url_obj->address.len  = tail-head;

        head = tail + 1;

        if( ( tail = strchr( head, '/' ) ) == NULL )
        {
            /* 案例:www.qianhai.com:8080 */
            http_url_obj->port.data = head;
            http_url_obj->port.len  = strlen(head);
            return 0;
        }

        /* 案例:www.qianhai.com:8080/doPay */
        http_url_obj->port.data = head;
        http_url_obj->port.len  = tail-head;
    }

    /* 路径 */
    http_url_obj->page.data = tail;
    http_url_obj->page.len  = strlen(tail);

    return 0;
}

int http_connect_service( hfp_http_url_t *http_url_obj )
{
    int ret;
    int sock_id;
    struct sockaddr_in sin;
    struct hostent *hp;
    char *address;

    if( ( sock_id = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
    {
        return -1;
    }

    address = malloc( sizeof(char) * (http_url_obj->address.len+1) );

    memcpy( address, http_url_obj->address.data, http_url_obj->address.len );
    address[ http_url_obj->address.len ] = '\0';

    bzero( &sin, sizeof(sin) );
    sin.sin_family = AF_INET;

    hp = gethostbyname( address );
    free(address);
    if( hp == NULL )
    {
        return -2;
    }
    sin.sin_addr = *((struct in_addr *)hp->h_addr);
    //sin.sin_addr.s_addr = inet_addr( "IP" );

    if( http_url_obj->port.data == NULL )
        sin.sin_port= htons( 80 );
    else
        sin.sin_port= htons( http_atoi(http_url_obj->port.data, http_url_obj->port.len) );

    ret = connect( sock_id, (struct sockaddr *)&sin, sizeof(sin) );
    if( http_alarm_flag == -1 )
    {
        close( sock_id );
        return RET_TIME_OUT;
    }
    if( ret < 0 )
    {
        close( sock_id );
        return -3;
    }

    return sock_id;
}

int http_write_socket( int sock_id, char *http_buffer,  int len)
{
    if( send( sock_id, http_buffer, len, 0 ) < 0 )
    {
        return -1;
    }

    return len;
}

int http_write_get( hfp_http_url_t *http_url_obj,
    char *http_buffer, int buffer_size, char *req, int req_len )
{
    int write_len=0;

    if( http_url_obj->page.len == 0 )
        sprintf( http_buffer,"GET /?%s HTTP/1.1\r\n",req);
    else
    {
        sprintf( http_buffer, "GET %*.*s?%s HTTP/1.1\r\n",
            http_url_obj->page.len, http_url_obj->page.len,
            http_url_obj->page.data, req);
    }
    write_len = strlen(http_buffer);

    sprintf( http_buffer+write_len,"Accept-Language: zh-cn\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Content-Type: application/x-www-form-urlencoded\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"User-Agent: cert\r\n");
    write_len += strlen(http_buffer+write_len);

    if( http_url_obj->port.len == 0 )
    {
        sprintf( http_buffer+write_len,"Host: %*.*s\r\n",
            http_url_obj->address.len, http_url_obj->address.len,
            http_url_obj->address.data);
    }
    else
    {
        sprintf( http_buffer+write_len,"Host: %*.*s:%*.*s\r\n",
            http_url_obj->address.len, http_url_obj->address.len,http_url_obj->address.data,
            http_url_obj->port.len, http_url_obj->port.len,http_url_obj->port.data);
    }
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Connection: close\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Cache-Control: no-cache\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"\r\n");
    write_len += 2;


    return write_len;
}

int http_write_post( hfp_http_url_t *http_url_obj,
    char *http_buffer, int buffer_size, char *req, int req_len )
{
    int write_len=0;

    if( http_url_obj->page.len == 0 )
        sprintf( http_buffer,"POST / HTTP/1.1\r\n");
    else
    {
        sprintf( http_buffer, "POST %*.*s HTTP/1.1\r\n",
            http_url_obj->page.len, http_url_obj->page.len,
            http_url_obj->page.data);
    }
    write_len = strlen(http_buffer);

    sprintf( http_buffer+write_len,"Accept-Language: zh-cn\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Content-Type: application/x-www-form-urlencoded\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"User-Agent: cert\r\n");
    write_len += strlen(http_buffer+write_len);

    if( http_url_obj->port.len == 0 )
    {
        sprintf( http_buffer+write_len,"Host: %*.*s\r\n",
            http_url_obj->address.len, http_url_obj->address.len,
            http_url_obj->address.data);
    }
    else
    {
        sprintf( http_buffer+write_len,"Host: %*.*s:%*.*s\r\n",
            http_url_obj->address.len, http_url_obj->address.len,http_url_obj->address.data,
            http_url_obj->port.len, http_url_obj->port.len,http_url_obj->port.data);
    }
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Content-Length: %d\r\n", req_len);
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Connection: close\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"Cache-Control: no-cache\r\n");
    write_len += strlen(http_buffer+write_len);

    sprintf( http_buffer+write_len,"\r\n");
    write_len += 2;

    sprintf( http_buffer+write_len, "%s", req);
    write_len += req_len;

    return write_len;
}

int http_write(char *meth, char *url, char *http_buffer, int buffer_size, char *req, int req_len)
{
    int ret;
    int sock_id;
    hfp_http_url_t http_url_obj;

    if( http_split_url( url, &http_url_obj ) != 0 )
    {
        return -1;
    }

    sock_id = http_connect_service( &http_url_obj);
    if( sock_id < 0 )
    {
        return -2;
    }

    if( strcmp( meth, "POST" ) == 0 )
        ret = http_write_post( &http_url_obj, http_buffer, buffer_size, req, req_len );
    else if(  strcmp( meth, "GET" ) == 0 )
        ret = http_write_get(  &http_url_obj, http_buffer, buffer_size, req, req_len );
    else
        ret = -3;

    if( ret<0 )
    {
        close(sock_id);
        return -4;
    }

    ret = http_write_socket( sock_id , http_buffer,  ret);
    if( ret<0 )
    {
        close(sock_id);
        return -5;
    }

    Eprintf("write http_buffer[%s]\n", http_buffer);

    return sock_id;
}

int http_read_socket( int sock_id, char *http_buffer, int buffer_size )
{
    int once_read_size;
    int read_len;

    once_read_size = ONCE_READ_LEN < buffer_size ? ONCE_READ_LEN : (buffer_size-1);

	read_len = recv( sock_id, http_buffer, once_read_size, 0 );

    if( http_alarm_flag == -1 )
    {
        return RET_TIME_OUT;
    }
    if( read_len < 0 )
    {
        return -1;
    }

    http_buffer[read_len]='\0';

    return read_len;
}

/* 返回已解析message长度，<0 失败 */
int http_read_start_line( hfp_http_object_t *http_obj, char *message, int message_len )
{
    char *tail;

    /* 状态行 HTTP-Version Status-Code Reason-Phrase\r\n
     *  012345678901234567890
     * "HTTP/1.1 200 OK\r\n"
     */
    if( message_len < 15 )
        return -1;

    //HTTP-Version:  HTTP/1.1
    if( message[8] != ' ' )
        return -1;

    http_obj->http_version.data = message;
    http_obj->http_version.len  = 8;
    
    Eprintf("http_version[%*.*s]\n", 
        http_obj->http_version.len,
        http_obj->http_version.len,
        http_obj->http_version.data);

    //Status-Code:200
    if( message[12] != ' ' )
        return -2;

    http_obj->status_code.data = message+9;
    http_obj->status_code.len  = 3;

    Eprintf("status_code[%*.*s]\n", 
        http_obj->status_code.len,
        http_obj->status_code.len,
        http_obj->status_code.data);

    //Reason-Phrase:
    tail = strstr( message+13, "\r\n");
    if( tail == NULL )
    {
        //Eprintf("[%s]\n", message+13);
        //int i;
        //for(i=0; i<12; i++ )
        //{
        //    Eprintf("[%#04x][%c]\n", message[13+i], message[13+i]);
        //}
        return -3;
    }

    http_obj->reason_phrase.data = message+13;
    http_obj->reason_phrase.len  = tail - (message+13);
    
    Eprintf("reason_phrase[%*.*s]\n", 
        http_obj->reason_phrase.len,
        http_obj->reason_phrase.len,
        http_obj->reason_phrase.data);

    return tail+2-message;
}

/* ret > 0 解析长度
 * -100    http报文没有读完
 * -1      错误
 * 0      报文头解析完毕
 */
int http_read_header_field( hfp_http_object_t *http_obj, char *message, int message_len )
{
    //每一个报头域都是由 (名字+":"+空格+值+\r\n) 组成，消息,报头域的名字是大小写无关的
    int len;
	char *tag_head, *tag_tail, *value_head, *value_tail;

    if( message[0] == '\r' && message[1] == '\n' )
        return 0;

    tag_head = message;

    /* 名字 */
    if( ( tag_tail = strchr( tag_head, ':' ) ) == NULL )
        return RET_REREAD_SOCKET;

    /* 值 */
    value_head = tag_tail+1;
    while( *value_head == ' ' ) value_head++;
    if( ( value_tail = strstr( value_head, "\r\n" ) ) == NULL )
        return RET_REREAD_SOCKET;

    len = tag_tail-tag_head;

    if( strncasecmp( tag_head, "Content-Length", len) == 0 )
    {
        http_obj->content_length.data = value_head;
        http_obj->content_length.len  = value_tail-value_head;
        http_obj->content_length_n = http_atoi(value_head, http_obj->content_length.len);
    }
    else if( strncasecmp( tag_head, "Location", len) == 0 )
    {
        http_obj->location.data = value_head;
        http_obj->location.len  = value_tail-value_head;
    }
    else if( strncasecmp( tag_head, "Transfer-Encoding", len) == 0 ) //Transfer-Encoding: chunked
    {
        http_obj->transfer_encoding.data = value_head;
        http_obj->transfer_encoding.len  = value_tail-value_head;
    }
    else
    {
        //放入other_head
    }
    
    Eprintf("%*.*s: %*.*s\n",
        len,len,tag_head,
        (int)(value_tail-value_head),(int)(value_tail-value_head),value_head); 

    return value_tail + 2 - message;
}

/* ret > 0 解析长度
 * -100    http报文没有读完
 * -1      错误
 * 0      报文头解析完毕
 */
int http_read_body_chunked( hfp_http_object_t *http_obj, char *message, int message_len )
{
    /* 分块编码数据部分格式：
     *   数据A长度（16进制字符串）+\r\n+数据A+\r\n+
     *   数据B长度（16进制字符串）+\r\n+数据B+\r\n+
     *   0+\r\n\r\n
     */
    int data_len;
    int chunked_len;   /* 分片长度= strlen(数据A长度) + strlen(数据A) + 4 */
    char *head, *tail;

    if( memcmp( message,"0\r\n\r\n", 5) == 0 )
        return 0;

    if( http_obj->body_num >= MAX_CHUNKED_NUM )
        return -1;

    /* 数据A长度 */
    head = message;
    if( ( tail = strstr( head, "\r\n") ) == NULL )
        return RET_REREAD_SOCKET;

    if( tail - head > MAX_CHUNKED_HEAD_LEN )
        return -2;

    data_len = http_hexstr2int( head, tail-head );
    chunked_len = tail-head + data_len + 4;

    if( message_len < chunked_len )
    {
        Eprintf("data_len[%d] chunked_len[%d] message_len[%d]\n",data_len,chunked_len,message_len);
        return RET_REREAD_SOCKET;
    }

    /* 数据A */
    head = tail+2;
//    if( ( tail = strstr( head, "\r\n") ) == NULL )
//        return RET_REREAD_SOCKET;

//    if( (tail-head) != data_len )
//        return -3;

    http_obj->body[http_obj->body_num].len  = data_len;
    http_obj->body[http_obj->body_num].data = head;
    http_obj->body_num++;

    return chunked_len;
}


/* res 接收缓存
 * res_size 接收缓存大小
 * time_out 超时时间
 * ret:  <0 失败
 */
int http_read( int sock_id, hfp_http_object_t *http_obj, char *http_buffer, int buffer_size )
{
	int ret;
	int read_len=0;         /* 已从socket中读取http长度 */
	int travel_len = 0;     /* 已解析长度 */

	memset( http_obj, 0, sizeof( hfp_http_object_t ) );

    ret = http_read_socket( sock_id, http_buffer, buffer_size );
    if( ret <= 0 )
        return -1;
    read_len = ret;

    /* 状态行 */
    ret = http_read_start_line( http_obj, http_buffer, read_len);
    if( ret < 0 )
    {
        Eprintf("http_read_start_line ret[%d] http_buffer[%s]\n",ret, http_buffer);
        return -6;
    }
    travel_len = ret;

    /* 消息报头 */
    while( 1 )
    {
        ret = http_read_header_field( http_obj, http_buffer+travel_len, read_len-travel_len);
        if( ret == RET_REREAD_SOCKET )  //http没读完
        {
            ret = http_read_socket( sock_id, http_buffer+read_len, buffer_size-read_len );
            if( ret <= 0 )
            {
                Eprintf("http_read_socket ret[%d]\n",ret);
                return -2;
            }
            read_len += ret;
            continue;
        }
        else if( ret < 0 )
            return -7;
        else if( ret == 0 ) /* 消息报头结束 */
        {
            travel_len += 2;
            break;
        }
            
        travel_len += ret;
    }

    /* 消息正文 */
    while( 1 )
    {
        if( http_obj->transfer_encoding.len >0 &&
            memcmp( http_obj->transfer_encoding.data, "chunked",http_obj->transfer_encoding.len) == 0 )
        {
            //分片传输
            ret = http_read_body_chunked( http_obj, http_buffer+travel_len, read_len-travel_len);
            Eprintf("http_read_body_chunked ret[%d]read_len[%d]\n",ret,read_len);  
        }
        else
        {
            if( (http_obj->content_length_n) > (read_len-travel_len) )
                ret = RET_REREAD_SOCKET;
            else
            {
                http_obj->body[0].len  = http_obj->content_length_n;
                http_obj->body[0].data = http_buffer+travel_len;
                http_obj->body_num = 1;
                ret = 0;
            }
        }

        if( ret == RET_REREAD_SOCKET )  //http没读完
        {
            ret = http_read_socket( sock_id, http_buffer+read_len, buffer_size-read_len );
            if( ret <= 0 )
            {
                Eprintf("http_read_socket ret[%d] http_buffer+travel_len[%s]\n",ret, http_buffer+travel_len);
                return -4;
            }
            read_len += ret;
            continue;
        }
        else if( ret < 0 )
        {
            Eprintf("http_read_body_chunked ret[%d]\n",ret);
            return -5;
        }
        else if( ret == 0 )
        {
            travel_len += 5;
            break;    /* 消息正文结束 */
        }

        travel_len += ret;            
    }

    //Eprintf("http_read_socket sock_id[%d] read_len[%d]travel_len[%d] http_buffer[%s]\n",
    //    sock_id, read_len, travel_len,http_buffer);

    return 0;
}

/* 客户端与服务端一次HTTP交互 */
int http_once_comm(hfp_http_object_t *http_obj, char *meth, char *url,
    char *http_buffer, int buffer_size, char *req, int req_len)
{
    int ret;
    int sock_id;

    sock_id = http_write( meth, url, http_buffer, buffer_size, req, req_len);
    if( sock_id < 0 )
    {
        return -1;
    }

    ret = http_read( sock_id, http_obj, http_buffer, buffer_size );
    close(sock_id);
    if( ret < 0 )
    {
        //Eprintf("http_read ret[%d]http_buffer[%s]\n",ret,http_buffer);
        Eprintf("http_read ret[%d]\n",ret);
        return -2;
    }

    return 0;
}

/* meth:  "POST","GET"
 * url: "http://192.168.45.33:8080/doPay"
 * req: 请求数据
 * res: 响应数据
 */
int My_Http_Client( char *meth, char *url, char *req, int req_len, char *res, int res_size, int time_out )
{
    int ret,i;
    char *location;
    char http_buffer[ HTTP_BUFFER_LEN ];  /* HTTP缓存空间 */
    hfp_http_object_t http_obj;

    res[0]='\0';

    /* 超时设置 */
    http_set_alarm( time_out );

    ret = http_once_comm( &http_obj, meth, url, http_buffer, sizeof(http_buffer), req, req_len);
    if( ret < 0 )
    {
        Eprintf("http_once_comm ret[%d]\n",ret);
        http_unset_alarm();
        return -1;
    }

    //重定向
    if( http_obj.location.data != NULL )
    {
        //"/do/login/"
        if( http_obj.location.data[0] == '/' )
        {

        }
        else
        {
            if( (location = malloc( sizeof(char)*(http_obj.location.len+1) ) ) == NULL )
            {
                http_unset_alarm();
                return -3;
            }
            memcpy( location, http_obj.location.data, http_obj.location.len);
            location[http_obj.location.len]='\0';
        }

        Eprintf("==========location [%s]\n", location);

        ret = http_once_comm( &http_obj, meth, location, http_buffer, sizeof(http_buffer), "", 0);
        free(location);
        if( ret < 0 )
        {
            http_unset_alarm();
            return -1;
        }
    }

    http_unset_alarm();

    for( i=0, ret=0; http_obj.body[i].len > 0; i++ )
    {
        if( (ret + http_obj.body[i].len) >= res_size )
        {
            Eprintf("ret + http_obj.body[i].len = %d res_size=%d\n",ret + http_obj.body[i].len,res_size);
            return -5;
        }
        sprintf(res+ret,"%*.*s",
            http_obj.body[i].len,http_obj.body[i].len,http_obj.body[i].data);
        ret += http_obj.body[i].len;
    }
    res[ret]='\0';

    return 0;
}

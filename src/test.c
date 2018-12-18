#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void test1(void)
{
    //char http_url[]="http://lmjloavcj.ngrok.cc:80/index.php/Buy/Wxpay/Wxpay";
    char http_url[]="http://wg.lcj999.com:80/";
    
    char http_req[]=" ";
    char http_res[1024];
    
    My_Http_Client( "POST", http_url, http_req, strlen(http_req), http_res, sizeof(http_res), 10);

    printf("http_res[%s]\n",http_res);
}

void test2(void)
{
    char http_url[]="http://121.40.136.150:12080/api/cics.htm";
    char http_req[]="userId=100115&realName=0BCC3E1150500DED4F06293C6F75CA9A&idCode=34B0387B70CF920FA4CE62C72FD4B48661B298B52BBF49C926D920057A0D3108&reqDate=2016-05-24 21:59:19&ts=1464098359&authType=01&sign=147b85f91e4a4193055292b1201eaae0";
    char http_res[1024];
    
    My_Http_Client( "POST", http_url, http_req, strlen(http_req), http_res, sizeof(http_res), 10);
    
    printf("http_res[%s]\n",http_res);
}

void test3(void)
{
    char http_url[]="http://api.puhuishuju.com/cert/bank/four";
    char http_req[]="uid=800102&realname=&idcard=152724198210122429&bankcard=4392260026578599&mobile=15149570614&sign=62533902F27231A7059E42050A71A340";
    char http_res[1024];
    
    My_Http_Client( "GET", http_url, http_req, strlen(http_req), http_res, sizeof(http_res), 10);
    
    printf("http_res[%s]\n",http_res);
}

void test4(void)
{
    char http_url[]="http://115.239.211.112";
    char http_req[]="uid=800102&realname=&&mobile=15149570614&sign=62533902F27231A7059E42050A71A340";
    char http_res[1024];
    
    My_Http_Client( "GET", http_url, http_req, strlen(http_req), http_res, sizeof(http_res), 10);
    
    printf("http_res[%s]\n",http_res);
}

void test5(void)
{
    char http_url[]="http://www.jieandata.com";
    char http_req[]="uid=800102&realname=&&mobile=15149570614&sign=62533902F27231A7059E42050A71A340";
    char http_res[1024];
    
    My_Http_Client( "GET", http_url, http_req, strlen(http_req), http_res, sizeof(http_res), 10);
    
    printf("http_res[%s]\n",http_res);
}



 
int main( int argc, char *argv[])
{
    test1();
    //test2();
    //test5();
    return 0;
}


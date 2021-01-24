#include <asm/socket.h>
#include <stdio.h>
#include <stdint-gcc.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "utils.h"
#include "type_defs.h"
#include <dirent.h>
#include "vdevs.h"
#define WEB_SERV_PORT 2313
#define MAX_WEB_CLIENTS 32
#define SERVER_STRING "Server: SagiNode Srv/0.1.0"
#define CLIENT_INBUF_SIZE	0x1000
#define CLIENT_OUTBUF_SIZE 	0x10000
#define WEBCLIENT_TIMEOUT 31000u
extern app_node_t* anetp;
static  node_t *Node;
extern vdevs_t         devList;

const char favico[]="<link rel=\"shortcut icon\" href=\"#\" />\r\n";

const char tetsPage[]= "<script language=\"JavaScript\"> function formToJson(form){var pass=form.pass.value; var ssid=form.ssid.value; var" \
		"jsonFormInfo=JSON.stringify({pass:pass, ssid: ssid}); window.alert(jsonFormInfo);}</script> <form onSubmit=\"event.preventDefault(); " \
		"formToJson(this);\"> <label class=\"label\">Network Name</label> <input type=\"text\" name=\"ssid\"/> <br/> <label>Password</label> " \
		"<input type=\"text\" name=\"pass\"/> <br/> <input type=\"submit\" value=\"Submit\"></form>";

const char test2[]="<!DOCTYPE html><html><head><title>Sagi Node page</title></head><body><h1>trying to get this</h1><p>Less than 5 minutes into this HTML tutorial andI've already created my first homepage!</p></body></html>";

const char frmPage[]="<!DOCTYPE html><html><head><title>A Sample HTML Form</title></head><body><h2 align=\"center\">Data & config Entry Form</h2><form method=\"post\" action=\"/\">" \
			"Enter your name: <input type=\"text\" name=\"username\"><br />Enter your password: <input type=\"password\" name=\"password\"><br />Which year?<input type=\"radio\" name=\"year\" value=\"2\" />Yr 1" \
    "<input type=\"radio\" name=\"year\" value=\"2\" />Yr 2<input type=\"radio\" name=\"year\" value=\"3\" />Yr 3<br />Subject registered:<input type=\"checkbox\" name=\"subject\" value=\"e101\" />E101" \
    "<input type=\"checkbox\" name=\"subject\" value=\"e102\" />E102<input type=\"checkbox\" name=\"subject\" value=\"e103\" />E103<br />Select Day:<select name=\"day\"><option value=\"mon\">Monday</option>" \
    "<option value=\"wed\">Wednesday</option><option value=\"fri\">Friday</option></select><br /><textarea rows=\"3\" cols=\"30\">Enter your special request here</textarea><br /><input type=\"submit\" value=\"SEND\" />" \
    "<input type=\"reset\" value=\"CLEAR\" /><input type=\"hidden\" name=\"action\" value=\"registration\" /></form></body></html>"	;

const char indexPg[]="<form method=\"post\" action=\"/\">"\
                        "Node Name %s<br />Interfaces found: <br />";
const char htmlentry[] = "<!DOCTYPE html><html>";

const char titlesBuilder[]="<head><title>%s</title></head><body>";


const char intfTable[] = "<style>table, th, td {  border: 2px solid black;  border-collapse: collapse;}th, td {  padding: 5px;}td{ text-align: center;}</style><table style=\"width:55%\"><tr><th>Index</th><th>Name</th><th>IP</th><th>MAC</th><th>WIRELESS</th></tr>";
const char intfTableR[] = "<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>";

const char selectScript[]="<script>\n document.getElementById(\"colMapList\").onchange = function() {fSelectedChanged()};\nfunction fSelectedChanged()\n{ \n\
  var e = document.getElementById(\"colMapList\"); \n\
  var value = e.options[e.selectedIndex].value; \n\
  var text = e.options[e.selectedIndex].text; \n\
  var str = document.getElementById(\"colMapList\").innerHTML;\n \
  var res = str.replace(\"selected\", \"\");\n \
  document.getElementById(\"colMapList\").innerHTML=res;\n \
  str = document.getElementById(\"colMapList\").innerHTML\n \
  var strT=\">\";\n \
  strT = strT.concat(text);\n \
  var n = str.indexOf(strT);\n \
  var ss1 = str.substring(0,n);\n \
  var ss2 = str.substring(n);\n \
  res = ss1;\n \
  res = res.concat(\" selected\",ss2);\n \
  document.getElementById(\"colMapList\").innerHTML=res;\n \
  }</script>";

int buildColorMappingLists(char* dst);

typedef struct
{
	char			*buf;
	char			*path;
	char			*answer;
	int				client_sock;
	pthread_t 	    clientHandle;
	uint32_t		valid;
}client_obj_t;
client_obj_t clients[MAX_WEB_CLIENTS];

typedef enum
{
	methPOST,
	methGET,
	methOTHER
}method_e;
typedef struct
{
	method_e meth;
	char* url;
	uint8_t url_len;
	uint8_t	http_ver;
}html_header_data_t;
typedef enum
{
    other_page_e=0,
    index_page_e = 1
}webPage_e;
typedef struct {
	char* resp;
	char* url;
	size_t flen;
	webPage_e wPageName;
}resp_index_t;

char* indexptr = NULL;
char* cfgptr=NULL;

resp_index_t resp_index[]= \
		{
			{.resp = indexPg, 	        .url = "/index\0", .flen=0, .wPageName = index_page_e},
            {.resp = NULL, 	            .url = "/index2\0", .flen=0},
			{.resp = NULL,  			.url = "/cfg2\0", .flen=0},
			{.resp = frmPage, 			.url = "/frm1\0", .flen=0},
			{.resp = frmPage, 			.url = "/cfg\0", .flen=0},
			{.resp = favico, 			.url = "/favicon.ico\0", .flen=0, .wPageName = other_page_e},
			{.resp = NULL,              .url = "/main", .flen = 0, .wPageName = other_page_e},
			{.resp = NULL,              .url = "\0", .flen = 0}

		};


typedef struct other_files__t* other_files_p;
typedef struct other_files__t
{
resp_index_t    file;
other_files_p   nxt;
}other_files_t;
other_files_p otherRes=NULL;

int intfToTabRow(node_interfaces_detail_t *ifc, char* t)
{
    return(sprintf(t, intfTableR, ifc->ifindex,  ifc->if_name,  ifc->ip_str , ifc->mac_str,ifc->isWireless?ifc->ssid1:"NO"  ));
}

int buildIndexPg(char* outf)
{
    int len=0;
    len +=sprintf(&outf[len], indexPg, Node->userName);
    int i;
    len += sprintf(&outf[len], intfTable);
    node_interfaces_detail_t *ifc = &Node->ifs.ifs[0];
    printf("indexPage list %d interfaces' data\n", Node->ifs.if_count);
    for(i=0;i<Node->ifs.if_count;i++)
    {
        printf("Interface[%d] index = %d\n", i, Node->ifs.ifs[i].ifindex);
        if(i == Node->ifs.curr_if_idx)len+=sprintf(&outf[len],"<b>");
        outf[len]='\0';
        //len +=sprintf(&outf[len], "\t%d %s @ %s wireless : %s<br />",1+ i, ifc->if_name, ifc->ip_str, ifc->isWireless?"YES":"NO");
        len+=intfToTabRow(ifc, &outf[len]);
        outf[len]='\0';
        if(i == Node->ifs.curr_if_idx)len+=sprintf(&outf[len],"</b>");
        ifc++;
        outf[len]='\0';
    }
    //

    len+=sprintf(&outf[len], "<tr><td><select><option>ALEX</option><option>Sagi</option></select></td></tr><br />");
     outf[len]='\0';
     len+=sprintf(&outf[len], "<tr><td><input type=\"radio\" name=\"r1\" value=\"1\" />RAD1\n<input type=\"radio\" name=\"r1\" value=\"2\" />RAD2\n</td></tr><br />");
     outf[len]='\0';
    len+=sprintf(&outf[len],"</table><br />Intensity Limit: <input type=text name=intLimit value=%d%% size=5 maxlenght=3>", Node->intLimit);
    outf[len]='\0';
    len+=sprintf(&outf[len],"<br />Node Name: <input type=text name=nodeName value=%s size=16 maxlenght=15>(leave blank for default)", Node->userName);
    outf[len]='\0';
    //nodeIP
    len+=sprintf(&outf[len],"<br />Node IP: <input type=text name=nodeIP value=%s size=16 maxlenght=15>", Node->ifs.ifs[Node->ifs.curr_if_idx].ip_str);
    outf[len]='\0';
    len+=buildColorMappingLists(&outf[len]);
    outf[len]='\0';
    len+=sprintf(&outf[len],"<br /><input type=submit value=SEND />\n%s\n</form></body></html>", selectScript);
    outf[len]='\0';

    return(len);
}

void buildVdevsTreeAsJSON(char* json)
{
    int i;
    char buf[4096];
    memset(buf, 0, 4096);
    mimas_out_dev_t *dev = &devList.devs[0];
    int len = sprintf(&buf[0],"{devices:[");
    for(i=0;i < MAX_VDEV_CNT; i++)
    {
        if(devList.devs[i].dev_com.dev_type != unused_dev)
        {
        len += sprintf(&buf[len],"{id:%u, devType:%u, startAddr:%u, endAddr:%u, startOffs:%u, endOffs:%u},"\
        ,i,dev->dev_com.dev_type, dev->dev_com.start_address, dev->dev_com.end_address, dev->dev_com.start_offset, dev->dev_com.end_offset);
        }
        dev++;

    }
    len+=sprintf(&buf[len-1],"]}");
    printf("\n%s\n", buf);

}

int buildColorMappingLists(char* dst)
{
   int len=0;
   len+=sprintf(&dst[len], "<br />\n<select id=\"colMapList\">\n");

    len+=sprintf(&dst[len],"<option value=\"0\"%s>RGB</option>\n",Node->outs[0]->colMap==0?" selected":"");
    len+=sprintf(&dst[len],"<option value=\"1\"%s>GRB</option>\n",Node->outs[0]->colMap==1?" selected":"");
    len+=sprintf(&dst[len],"<option value=\"2\"%s>RBG</option>\n",Node->outs[0]->colMap==2?" selected":"");
    len+=sprintf(&dst[len],"<option value=\"3\"%s>GBR</option>\n",Node->outs[0]->colMap==3?" selected":"");
    len+=sprintf(&dst[len],"<option value=\"4\"%s>BRG</option>\n",Node->outs[0]->colMap==4?" selected":"");
    len+=sprintf(&dst[len],"<option value=\"5\"%s>BGR</option>\n</select><br />",Node->outs[0]->colMap==5?" selected":"");
    return(len);

}

int buildPg(const char* title, const char* body, char* outf)
{
    int len =0;
    printf("Build index with body(%d):\n----------------------------------\n%s\n--------------------------------------\n",strlen(body),body);
    outf[0]='\0';
    len += sprintf(&outf[len], htmlentry);
    outf[len] = '\0';
    len += sprintf(&outf[len], titlesBuilder, title);
    outf[len] = '\0';
    len += sprintf(&outf[len], "%s",body);
    outf[len] = '\0';
    printf("Index page built:\n%s\n", outf);
    return(len);
}

static int getHttpResp(char* u)
{
	int i=0;
	while(1)
	{
        if(resp_index[i].url[0] == '\0')return(-1);
		if( strcmp(resp_index[i].url, u) == 0 )return(i);
		i++;
	}

	return -1;
}
static other_files_p getOtherRes(char* u)
{
	if(otherRes!=NULL)
	{
        other_files_p f = otherRes;
        while(f)
        {
            printf("File '%s' len %u\n",f->file.url, f->file.flen);
            if((f->file.url != NULL) &&(strncmp(f->file.url, u, strlen(f->file.url)) ==0))
            {
                printf("Matching resource file '%s' found!\n", f->file.url);
                return(f);
            }

            f = f->nxt;
        }
	}
	return(NULL);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client, char* url)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.1 404 NOT FOUND\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found(404)</TITLE>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>%s <br />could not fulfill your request...", SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<br />The resource specified : %s is unavailable or nonexistent.\r\n", (url == NULL?"":url) );
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}


/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];
 printf("BAD REQ\n");
 int n;
 n=sprintf(buf, "HTTP/1.1 400 BAD REQUEST\r\n");
 buf[n]='\0';
 send(client, buf, sizeof(buf), 0);
 n=sprintf(buf, "Content-type: text/html\r\n");
 buf[n]='\0';
 send(client, buf, sizeof(buf), 0);
 n=sprintf(buf, "\r\n");
 buf[n]='\0';
 send(client, buf, sizeof(buf), 0);
 n=sprintf(buf, "<P>Your browser sent a bad request, ");
 buf[n]='\0';
 send(client, buf, sizeof(buf), 0);
 n=sprintf(buf, "such as a POST without a Content-Length.\r\n");
 buf[n]='\0';
 send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];

 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))
 {
  send(client, buf, strlen(buf), 0);
  fgets(buf, sizeof(buf), resource);
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];
 int n;
 n = sprintf(buf, "HTTP/1.1 500 Internal Server Error\r\n%s\r\n",SERVER_STRING);
 send(client, buf, n, 0);
 n = sprintf(buf, "Content-type: text/html\r\n\r\n");
 send(client, buf, n, 0);
 n = sprintf(buf, "<P>Error Too many clients connected.<br>Up to %d allowed.<br>Please try in about %d seconds\r\n", MAX_WEB_CLIENTS, WEBCLIENT_TIMEOUT);
 send(client, buf, n, 0);
}


/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, MSG_WAITALL);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';

 return(i);
}

int httpOKsend(char* buf, int clientFd)
{
    int n;
    char *pt;
    char lBuf[256]={'\0'};
    if(buf == NULL)
    {
        pt = &lBuf[0];
    }
    else
    {
        pt = buf;
    }
    *pt='\0';
    n = snprintf(pt, 512, "HTTP/1.1 200 OK\r\n%s\r\n",SERVER_STRING);
    if(buf == NULL)
    {
        printf("\t\t\t\t===> %s\n",pt);
        send(clientFd, pt, n, 0);
        return(0);
    }
    return(n);
}
/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int id, const char *filename)
{
 int	client = clients[id].client_sock;
 char *buf = clients[id].answer;
 char hd[512];
 int n,len;
 //(void)filename;  /* could use filename to determine file type */
 n =  httpOKsend(NULL, client);
 len=0;
 if(filename!=NULL)
 {
     strncpy(buf,filename, CLIENT_OUTBUF_SIZE);
     len = strlen(buf);
 }
 hd[n]='\0';
 n = snprintf(hd, 512, "Content-Length: %d\r\n",len);
 hd[n]='\0';
 //prn("send_client",hd);
 printf("\t\t\t\t===> %s\n",hd);
 send(client, hd, n, 0);
 n = snprintf(hd, 512, \
		 "Content-Type: text/html\r\n" \
		 "Keep-Alive: timeout=%u, max=50\r\n" \
		 "Connection: Keep-Alive\r\n\r\n", WEBCLIENT_TIMEOUT);

 hd[n]='\0';
 //prn("send_client",hd);
 printf("\t\t\t\t===> %s\n",hd);
 send(client, hd,n, 0);
 if(len>0)
 {
	// prn("send_client",buf);
	 printf("\t\t\t\t===> %s\n",buf);
	 send(client, buf, strlen(buf), 0);
 }
}


void headersOther(int id, const other_files_p file)
{
 int	client = clients[id].client_sock;
 char *buf = clients[id].answer;
 char hd[512];
 int n,len;
 char* filename = file->file.url;
 char ftype[16];
 memset(ftype,0,16);
 if( strstr(filename, ".css") !=NULL)
 {
    strcat(ftype,"css");
 }
 else{
    if(strstr(filename,".js")!=NULL)
    {
    strcat(ftype,"javascript");
    }
 }
 //(void)filename;  /* could use filename to determine file type */
 n =  httpOKsend(NULL, client);
 len=0;
 if(file->file.resp!=NULL)
 {
     strncpy(buf,file->file.resp, CLIENT_OUTBUF_SIZE);
     len = strlen(buf);
 }
 hd[n]='\0';
 n = snprintf(hd, 512, "Content-Length: %u\r\n",len);
 hd[n]='\0';
 //prn("send_client",hd);
 printf("\t\t\t\t===> %s\n",hd);
 send(client, hd, n, 0);
 n = snprintf(hd, 512, \
		 "Content-Type: text/%s\r\n" \
		 "Keep-Alive: timeout=%u, max=50\r\n" \
		 "Connection: Keep-Alive\r\n\r\n", ftype,WEBCLIENT_TIMEOUT);

 hd[n]='\0';
 //prn("send_client",hd);
 printf("\t\t\t\t===> %s\n",hd);
 send(client, hd,n, 0);
 if(len>0)
 {
	// prn("send_client",buf);
	 printf("\t\t\t\t===> %s\n",buf);
	 send(client, buf, strlen(buf), 0);
 }
}

int webSockFd;
/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
static int webServSockInit(uint16_t port)
{
 int httpd = 0;
 int optVal, res;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
 {
  perror("webServ socket");

  return -1;
 }
 optVal=1;
 res = setsockopt(httpd,SOL_SOCKET,SO_KEEPALIVE,(void*)&optVal, sizeof(int));
 if(res)perror("sockOpt SO_KEEPALIVE error:");
 res = setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optVal , sizeof(int));
  if(res)perror("sockOpt SO_REUSEADDR error:");
/* setsockopt(httpd,SOL_SOCKET,SO_REUSEADDR,(void*)&optVal, sizeof(int));*/
 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
 {
    perror("webServ bind");
    return(-1);
 }
 if (port == 0)  /* if dynamically allocating a port */
 {
  socklen_t namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   perror("webServ getsockname");
   port = ntohs(name.sin_port);
 }
 if (listen(httpd, 8) < 0)
 {
  perror("webServ listen failed!");
  httpd =-1;
 }
 return(httpd);
}
#define closeClient(idx) \
    printf("client %u error %d\n", idx, errno); \
            perror("clentWorker"); \
            close(clients[idx].client_sock);\
            clients[idx].client_sock = -1; \
            pthread_exit(NULL);

static void *clientWorker(void* d)
{
    int id = (int)d;
    int n, i, j, httpRespIndex;
    char *pt, *pt2, *url, *usrtoken;
    int sck = clients[id].client_sock;
    other_files_p f  = NULL;
    html_header_data_t hd;
    printf("----------------------------------------------------------------------------\nclientWorker %u created\n", id);
    char* buf = clients[id].buf;
    while(1)
    {
        errno =0;
        n = recv(sck, buf, 0x1000, 0);
        if(n<=0)
        {
            closeClient(id);
        }
        buf[n] = '\0';
        pt = strtok(buf,"\r\n");
        while(pt!=NULL)
		 {
			 printf("\t\t<== %s\n",pt);
			 if(strncmp("GET ",pt,4)==0)
			 {
				 hd.meth = methGET;
				 pt2=strchr(&pt[4],' ');
				 if(pt2!=NULL)
				 {
					 *pt2='\0';
					 hd.url=&pt[4];
					 url = hd.url;
					 pt2++;
					 printf("parse: Path = '%s ' req = '%s'\n",url, pt2 );
					 usrtoken = strchr(url, '?');
					 if(usrtoken!=NULL){
					 *usrtoken = '\0';usrtoken++;
					 }
					 if(strncmp("HTTP/1.1",pt2,8)==0)
					 {
                         if(url[strlen(url)-1] == '/')url[strlen(url)-1]='\0';
						 httpRespIndex = getHttpResp(url);
						 if(httpRespIndex ==-1)
						 {
                             f= getOtherRes(&url[1]);
                         }
                         if((f==NULL)&&(httpRespIndex ==-1))
                         {

                             printf("resp_index: unknown request %s\n",url);
                             not_found(sck, url);
                             closeClient(id);
                         }

						 else
						 {
                            //printf("resp_index:  found resp %d for request %s\n",httpRespIndex,url);
                            if(httpRespIndex !=-1)
                            {
                                if( resp_index[httpRespIndex].wPageName== index_page_e )
                                {
                                    char ibody[4096];
                                    char ipg[4096];
                                    volatile int len=0;
                                    memset(ibody,0,4096);
                                    memset(ipg,0,4096);
                                    len = buildIndexPg(ibody);
                                    len = buildPg("PixLed NODE info page",ibody,ipg);
                                    headers(id,ipg);
                                }
                                else
                                {
                                    headers(id,resp_index[httpRespIndex].resp);
                                }
                            }
                            else
                            {
                                char *dot = strchr(url,'.');
                                if(dot)
                                {
                                    dot++;
                                    if(  (strncmp(dot,"html",4)==0))
                                    {
                                        headers(id,f->file.resp);
                                    }
                                    else if ((strncmp(dot,"css",3)==0) || (strncmp(dot,"js",2)==0))
                                    {
                                        headersOther(id,f);
                                    }

                                    else
                                    {
                                    printf("error unknown file type %s\n", url);
                                    }
                                }

                            }


						 }
						 /*
						 if(strncmp("/cfg\0",url,5)==0) headers(idx,test2);
						 else if(strncmp("/\0",url,2)==0)
						 {
							 headers(idx,wifi_info_resp);
						 }
						 else if(strcmp("/favicon.ico\0",url)==0)headers(idx,favico);
						 else not_found(client);*/
					 }
					 else
					 {
						 printf( "HTTP version error, got %s\n",pt2);
					 }
				 }
			 }
			 else if(strncmp("POST ",pt,5)==0)
			 {
                char *pt2 = pt;
				printf("Got a POST\n");
				hd.meth = methPOST;
				httpOKsend(NULL, id);
				int refresh=0;
				do
				{
                    pt=strtok(NULL,"\r\n");
                    if(pt)
                    {
                        printf("POST read: %s\n", pt);
                        if((pt2 = strstr(pt,"intLimit"))!=NULL)
                        {
                            pt2 = strchr(pt2,'=');
                            if(pt2)
                            {
                                pt2++;
                                anetp->artnode->intLimit = atoi(pt2);
                                if(anetp->artnode->intLimit > 100)anetp->artnode->intLimit  = 100;
                                printf("INTLIM set to %d\n", anetp->artnode->intLimit );
                                refresh++;
                            }
                        }
                        if((pt2 = strstr(pt,"nodeIP"))!=NULL)
                        {
                            if(pt2)
                            {
                                pt2 = strchr(pt2,'=');
                                pt2++;
                                if(strcmp(pt2, Node->ifs.ifs[Node->ifs.curr_if_idx].ip_str)!=0)
                                {
                                    printf("Must change IP from %s to %s\n", Node->ifs.ifs[Node->ifs.curr_if_idx].ip_str, pt2);
                                    int rc = setIP(pt2, Node->current_if_idx);
                                    if(rc!=0)
                                    {
                                        printf("Failed to set new Node IP. rc: %d, err: %d\n",rc, errno);
                                        errno=0;
                                    }
                                    else
                                    {
                                        printf("Changed Node IP to %s ... OK!\n",Node->ifs.ifs[Node->ifs.curr_if_idx].ip_str);
                                    }
                                    refresh++;
                                }

                            }
                        }
                        if((pt2 = strstr(pt,"nodeName"))!=NULL)
                        {
                            if(pt2)
                            {
                                char *pt3;
                                pt2 = strchr(pt2,'=');
                                pt2++;
                                memset(anetp->artnode->userName, 0, sizeof(anetp->artnode->userName));
                                pt3 = strchr(pt2,'&');
                                if(pt3)*pt3='\0';
                                if(strlen(pt2)==0)
                                {
                                    strcpy(anetp->artnode->userName, anetp->artnode->nodeName);
                                }
                                else
                                {
                                    strncpy(anetp->artnode->userName, pt2, sizeof(anetp->artnode->userName) -1);
                                }
                                printf("nodeName set to %s\n", anetp->artnode->userName );
                                refresh++;
                            }
                        }

                        if(refresh)
                        {
                            char ibody[1024];
                            char ipg[2048];
                            volatile int len=0;
                            memset(ibody,0,1024);
                            memset(ipg,0,2048);
                            len = buildIndexPg(ibody);
                            len = buildPg("PixLed NODE info page",ibody,ipg);
                            headers(id,ipg);
                        }
                    }
				}while(pt);
			 }
			 else
			 {
				 if(strcmp("Connection: Close\n",pt)==0)
				 {
					 printf("Client side Close request.Closing client %u\n",sck);
					 closeClient(id);
				 }
				 else
				 printf("parse:  more junk: %s\n",pt );
			 }
			 pt=strtok(NULL,"\r\n");
		 }

    }
}

static int createClient(uint32_t id)
{
	char clientName[16];
	int optVal;
	int rc;
	optVal=1;
	snprintf(clientName,16,"client@%u",clients[id].client_sock);
	printf("Incoming connection for client %s",clientName);
	setsockopt(clients[id].client_sock,SOL_SOCKET,SO_KEEPALIVE,(void*)&optVal, sizeof(int));
	if(0!=setSockTimout(clients[id].client_sock, 31000u))
	{
		printf( "Failed to set sock timeout for client %s",clientName);
	}
	clients[id].buf =(char*)malloc(0x1000);
	clients[id].answer =(char*)malloc(CLIENT_OUTBUF_SIZE);
	clients[id].path = (char*)malloc(512);
	memset(clients[id].buf, 0, 0x1000);
	memset(clients[id].answer, 0, CLIENT_OUTBUF_SIZE);

	rc = pthread_create(&clients[id].clientHandle, NULL, clientWorker,(void*)id);


	if(rc != 0)
	{
		printf("Error when creating client %s", clientName);
		perror("createClient");
		free(clients[id].buf);
		free(clients[id].answer);
		free(clients[id].path);
		return(-1); /* failure */
	}
	return 0; /*success */
}

static int findFreeClient(int sockNum)
{
	int i;
	/*eTaskState state;
	for(i=0;i<MAX_WEB_CLIENTS;i++)
	{
		if(clients[i].valid == 0)break;
		if(clients[i].clientHandle!=NULL)
		{
			state = eTaskGetState(clients[i].clientHandle);
			if(state == eSuspended)
			{
				vTaskDelete(clients[i].clientHandle);
				clients[i].clientHandle = NULL;
				clients[i].client_sock = -1;
			}
		}
	}*/
	for(i=0;i<MAX_WEB_CLIENTS;i++)
	{
		if(clients[i].valid==0) return(-1);
		if(clients[i].clientHandle==NULL)
		{
			clients[i].client_sock = sockNum;
			return(i);
		}
	}
	return(-2);
}
static void initClients(void)
{
    int i;
    for(i=0;i<MAX_WEB_CLIENTS;i++)
    {
        clients[i].answer = NULL;
        clients[i].buf = NULL;
        clients[i].client_sock = -1;
        clients[i].valid = 1;
        clients[i].clientHandle = 0;
    }
}
char* openFile(const char* fname, size_t* flen)
{
   FILE* findex=fopen(fname,"r");
   if(findex == 0)
   {
    printf("file %s not found\n",fname);
    return(NULL);
   }
   //FILE* fcfg = fopen("web_rsrc/cfg.html","r");
   struct stat sb;
   off_t offset=0, pa_offset=0;
   size_t length;
   ssize_t s;
   char* addr;
   int rc = fstat(findex->_fileno,&sb);
   if(rc!=0)
   {
    printf("fileMap %s failed. rc = %d\n",fname,rc);
    fclose(findex);
    return(NULL);
   }
   length = sb.st_size;
   addr = mmap(NULL, length + offset - pa_offset, PROT_READ, MAP_PRIVATE, findex->_fileno, pa_offset);
   if (addr == MAP_FAILED)
   {
    printf("mapping of %s failed\n",fname);
    fclose(findex);
    return NULL;
   }
   fclose(findex);
   *flen = length;

   return(addr);
}

int dirFiles(other_files_p dst)
{

DIR *dir;
struct dirent *ent;
if ((dir = opendir ("./web_rsrc")) != NULL)
{
  /* print all the files and directories within directory */
  while ((ent = readdir (dir)) != NULL)
  {
    dst->nxt = NULL;
    printf ("%s\n", ent->d_name);
    if( (*(int*)ent->d_name != (256*'.' + '.')) && (*(int*)ent->d_name != ('.')))
    {
        printf("Append file %s to tree\n", ent->d_name);
        dst->file.url = strdup( ent->d_name);
        dst->nxt= (other_files_p)malloc(sizeof(other_files_t));
        dst = dst->nxt;
    }
  }
  closedir (dir);
  dst->nxt = NULL;
  return(0);
}
else
{
  /* could not open directory */
  perror ("");
  return -1;
}
}
other_files_p openFiles(void)
{
    int i;
    char fn[64];
    char* pt;
    size_t flen;
    i = 0;

    while(1)
    {
        if(resp_index[i].url[0] == '\0')break;
        if(resp_index[i].resp==NULL)
        {
            fn[0]='\0';
            sprintf(fn,"web_rsrc%s\0",resp_index[i].url);
            strcat(fn,".html");
            pt = openFile(fn, &flen);
            if(pt!=NULL)
            {
                resp_index[i].resp = pt;
            }
        }
        else{
        flen = strlen(resp_index[i].resp);
        }
        resp_index[i].flen = flen;
        i++;
    }
    volatile  other_files_p otherFiles = (other_files_t*)malloc(sizeof(other_files_t));
    memset(otherFiles,0,sizeof(other_files_t));
    dirFiles(otherFiles);
    other_files_p hd = otherFiles;
    while(hd!=NULL)
    {

        fn[0]='\0';
        sprintf(fn,"web_rsrc/%s\0",hd->file.url);
        pt = openFile(fn, &flen);
        if(pt!=NULL)
        {
            hd->file.resp = pt;
            hd->file.flen = flen;
        }
        hd = hd->nxt;
    }
    return(otherFiles);
}

int webServStart(void)
{
	//return;
    Node = anetp->artnode;
    otherRes = openFiles();
    buildVdevsTreeAsJSON(NULL);
    initClients();
    webSockFd = webServSockInit(WEB_SERV_PORT);
    if(webSockFd<1)return(-1);
    int clientfd, cid;
    uint16_t clientId=0;
    struct sockaddr_in clientName;
    socklen_t client_name_len;
    printf("Web Server started.. listening on %u\n", WEB_SERV_PORT);
    while(1)
    {
        client_name_len = sizeof(clientName);
        clientfd = accept(webSockFd,&clientName, &client_name_len);
        if(clientfd==-1)
        {


        }
        else
        {
            cid =findFreeClient(clientfd);
            if(cid>=0)
            {
                createClient(cid);
            }
        }
    }
}

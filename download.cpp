#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fcntl.h>

#define SOCKET_ERROR        -1
#define BUFFER_SIZE         1
#define HOST_NAME_SIZE      255
#define MAX_MSG_SZ   	    1024

using namespace std;

const char* buildGetQuery(string host, string page)
{
	string query;
	query = "GET " + page + " HTTP/1.0\r\nHost: " + host + "\r\n\r\n";
	return query.c_str();
}


// Determine if the character is whitespace
bool isWhitespace(char c)
{
    switch (c)
    {
        case '\r':
        case '\n':
        case ' ':
        case '\0':
            return true;
        default:
            return false;
    }
}

// Strip off whitespace characters from the end of the line
void chomp(char *line)
{
    int len = strlen(line);
    while (isWhitespace(line[len]))
    {
        line[len--] = '\0';
    }
}

// Read the line one character at a time, looking for the CR
// You dont want to read too far, or you will mess up the content
char * GetLine(int fds)
{
    char tline[MAX_MSG_SZ];
    char *line;
    
    int messagesize = 0;
    int amtread = 0;
    while((amtread = read(fds, tline + messagesize, 1)) < MAX_MSG_SZ)
    {
        if (amtread > 0)
            messagesize += amtread;
        else
        {
            perror("Socket Error is:");
            fprintf(stderr, "Read Failed on file descriptor %d messagesize = %d\n", fds, messagesize);
            exit(2);
        }
        //fprintf(stderr,"%d[%c]", messagesize,message[messagesize-1]);
        if (tline[messagesize - 1] == '\n')
            break;
    }
    tline[messagesize] = '\0';
    chomp(tline);
    line = (char *)malloc((strlen(tline) + 1) * sizeof(char));
    strcpy(line, tline);
    //fprintf(stderr, "GetLine: [%s]\n", line);
    return line;
}
    
// Change to upper case and replace with underlines for CGI scripts
void UpcaseAndReplaceDashWithUnderline(char *str)
{
    int i;
    char *s;
    
    s = str;
    for (i = 0; s[i] != ':'; i++)
    {
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] = 'A' + (s[i] - 'a');
        
        if (s[i] == '-')
            s[i] = '_';
    }
    
}


// When calling CGI scripts, you will have to convert header strings
// before inserting them into the environment.  This routine does most
// of the conversion
char *FormatHeader(char *str, char *prefix)
{
    char *result = (char *)malloc(strlen(str) + strlen(prefix));
    char* value = strchr(str,':') + 2;
    UpcaseAndReplaceDashWithUnderline(str);
    *(strchr(str,':')) = '\0';
    sprintf(result, "%s%s=%s", prefix, str, value);
    return result;
}

// Get the header lines from a socket
//   envformat = true when getting a request from a web client
//   envformat = false when getting lines from a CGI program

void GetHeaderLines(vector<char *> &headerLines, int skt, bool envformat)
{
    // Read the headers, look for specific ones that may change our responseCode
    char *line;
    char *tline;
    
    tline = GetLine(skt);
    while(strlen(tline) != 0)
    {
        if (strstr(tline, "Content-Length") || 
            strstr(tline, "Content-Type"))
        {
            if (envformat)
                line = FormatHeader(tline, "");
            else
                line = strdup(tline);
        }
        else
        {
            if (envformat)
                line = FormatHeader(tline, "HTTP_");
            else
            {
                line = (char *)malloc((strlen(tline) + 10) * sizeof(char));
                sprintf(line, "HTTP_%s", tline);                
            }
        }
        
        headerLines.push_back(line);
        free(tline);
        tline = GetLine(skt);
    }
    free(tline);
}

int  main(int argc, char* argv[])
{
    int hSocket;                 /* handle to socket */
    struct hostent* pHostInfo;   /* holds info about a machine */
    struct sockaddr_in Address;  /* Internet socket address stuct */
    long nHostAddress;
    string hostName;
    string pagePath;
    int portNumber;
    bool debugging = false;
    
    vector<char*> headerLines;
	char buffer[MAX_MSG_SZ];
	char contentType[MAX_MSG_SZ];
    
	//host port page
	
	char arg1[strlen(argv[1])];
	strcpy(arg1, argv[1]); /*don't want to change the actual arg, just need it as a string*/
	
    if((argc < 4) || (argc > 5))
    {
      	perror("Usage: download [-d] host-name host-port page-path");
        return 0;
    }
   	else if (argc==4)
    {
      	hostName = argv[1];
        portNumber = atoi(argv[2]);
        pagePath = argv[3];
    }
    else if (argc==5)
    {
    	debugging = true;
    	hostName = argv[2];
    	portNumber = atoi(argv[3]);
    	pagePath = argv[4];
    }
    if (portNumber != 80)
    {
    	perror("Usage: download [-d] host-name host-port page-path");
        return 0;
    }

	if (debugging)
    	cout << "Making a socket" << endl;
    /* make a socket */
    hSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

    if(hSocket == SOCKET_ERROR)
    {
    	cout << "Could not make a socket" << endl;
        return 0;
    }
    
    /* get IP address from name */
    pHostInfo=gethostbyname(hostName.c_str());
    /* copy address into long */
    memcpy(&nHostAddress,pHostInfo->h_addr,pHostInfo->h_length);

    /* fill address struct */
    Address.sin_addr.s_addr=nHostAddress;
    Address.sin_port=htons(portNumber);
    Address.sin_family=AF_INET;
    
    if (debugging)
    	cout << "Connecting to " << hostName << " on port " << portNumber << endl;

    /* connect to host */
    if(connect(hSocket,(struct sockaddr*)&Address,sizeof(Address)) 
       == SOCKET_ERROR)
    {
        cout << "Could not connect to host" << endl;
        return 0;
    }
    
    //download the specified URL:
    // 1. Send GET command on the socket: 
    	//"GET /path/to/file/index.html HTTP/1.0\r\nHost: host.com:80\r\n\r\n"
    // 2. Recieve HTTP response
    	//examine response for Content-Length header
    // 3. Read content of response
    
    if (debugging)
    	cout << "Sending GET command:" << endl;
    
    const char* getQuery = buildGetQuery(hostName, pagePath);
    
	if (debugging)
	{
   		cout << "========== GET query ==========" << endl;
   		cout << getQuery;
    	cout << "===============================" << endl;
    }
    
	int n = write(hSocket, getQuery, strlen(getQuery));
	if (n<0)
	{
		cout << "Error writing to socket" << endl;
		return 0;
	}
	
	if (debugging)
		cout << "Recieving HTTP response:" << endl;

	//read status line
	char* startLine = GetLine(hSocket);
	
	if (debugging)
		cout << "Status line: " << startLine << endl;
	
	//read headers
	GetHeaderLines(headerLines, hSocket, false);
	
	char* contentLength;
	
	if (debugging)
	{
		cout << endl << "========== HTTP HEADERS ==========" << endl;
		
		for (int i = 0; i < headerLines.size(); i++)
		{
			printf("[%d] %s\n",i,headerLines[i]);
			if(strstr(headerLines[i], "Content-Length")) 
			{
				sscanf(headerLines[i], "Content-Length: %s", contentLength);
			}
		}
		cout << "==================================" << endl << endl;
  	}
	
	int numContentLength = atoi(contentLength);
	
	//now read the rest of the file
	
	int rval;
	while ((rval = read(hSocket, buffer, MAX_MSG_SZ)) > 0)
	{
		write(1,buffer,rval);
	}

	cout << endl << "Closing socket" << endl;
	
    /* close socket */                       
    if(close(hSocket) == SOCKET_ERROR)
    {
    	cout << "Could not close socket" << endl;
        return 0;
    }
    return 0;
}
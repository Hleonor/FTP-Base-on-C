#include <WinSock2.h>
#include "size.h"
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

// 创建线程时传递的数据结构,内含控制连接套接字和客户端地址信息:
struct threadData
{
    SOCKET tcps;
    sockaddr_in clientaddr;
};

// 全局函数声明:
// FTP初始化,创建一个侦听套接字:
int InitFTP(SOCKET *pListenSock); // 初始化FTP服务器套接字
int InitDataSocket(SOCKET *pDatatcps, SOCKADDR_IN *pClientAddr); // 初始化数据连接套接字
int ProcessCmd(SOCKET tcps, CmdPacket *pCmd, SOCKADDR_IN *pClientAddr); // 处理客户端命令
int SendRspns(SOCKET tcps, RspnsPacket *prspns); // 发送回复报文
int RecvCmd(SOCKET tcps, char *pCmd); // 接收命令报文
int SendFileList(SOCKET datatcps); // 发送文件列表
int SendFileRecord(SOCKET datatcps, WIN32_FIND_DATA *pfd); // 发送文件记录
int SendFile(SOCKET datatcps, FILE *file); // 发送文件
int RecvFile(SOCKET datatcps, char *filename); // 接收文件
int FileExists(const char *filename); // 判断文件是否存在

// 线程函数,参数包括相应控制连接的套接字:
DWORD WINAPI ThreadFunc(LPVOID lpParam)
{
    SOCKET tcps;
    sockaddr_in clientaddr;
    tcps = ((struct threadData *) lpParam)->tcps; // 强转为threadData结构指针,取出控制连接套接字
    clientaddr = ((struct threadData *) lpParam)->clientaddr; // 强转为threadData结构指针,取出客户端地址信息
    printf("socket的编号是：%u.\n", tcps);

    //发送回复报文给客户端,内含命令使用说明:
    // 先打印客户端的IP地址和端口号:
    printf("Serve client %s:%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
    // 定义一个服务器的回复报文
    RspnsPacket rspns = {OK,
                         "欢迎使用该系统\n"
                         "可以使用的命令:\n"
                         "ls\t<<展示当前目录下的文件(夹)>>\n"
                         "pwd\t<<展示当前目录的绝对路径>>\n"
                         "cd\t<<切换目录命令，格式{cd \"目录名\"}>>\n"
                         "down\t<<下载命令，格式{down \"文件名\"}>>\n"
                         "up\t<<上传命令，格式{up \"文件名\"}>>\n"
                         "quit\t<<退出系统>>\n"
    };
    // 将这个回复报文发送给客户端
    SendRspns(tcps, &rspns);

    // 循环获取客户端命令报文并进行处理
    for (;;)
    {
        CmdPacket cmd;
        if (RecvCmd(tcps, (char *) &cmd) == 0) // 如果函数返回值为0，说明客户端已经退出或出现错误
        {
            break;
        }
        if (ProcessCmd(tcps, &cmd, &clientaddr) == 0) // 如果函数返回值为-7，说明客户端已经退出或出现错误
        {
            break;
        }
    }
    //线程结束前关闭控制连接套接字:
    closesocket(tcps);
    delete lpParam;
    return 0;
}

int main(int argc, char *argv[])
{
    SOCKET tcps_listen;  // FTP服务器控制连接侦听套接字
    struct threadData *pThInfo; // 线程结构体指针

    if (InitFTP(&tcps_listen) == 0) // 如果初始化失败，InitFTP函数返回-1
    {
        // FTP初始化
        return 0;
    }
    printf("FTP服务器开始监听端口号：%d......\n", CMD_PORT);

    //循环接受客户端连接请求,并生成线程去处理:
    while (1)
    {
        pThInfo = NULL;
        pThInfo = new threadData;
        if (pThInfo == NULL) // 如果申请空间失败，new返回NULL，但是一般不会出现这种情况
        {
            printf("为新线程申请空间失败。\n");
            continue;
        }

        int len = sizeof(struct threadData);
        // 等待接受客户端控制连接请求
        pThInfo->tcps = accept(tcps_listen, (SOCKADDR *) &pThInfo->clientaddr, &len);

        // 创建一个线程来处理相应客户端的请求:
        DWORD dwThreadId, dwThrdParam = 1;
        HANDLE hThread;

        hThread = CreateThread(
                NULL,               // 无需安全性的继承
                0,                    // 默认线程栈大小
                ThreadFunc,            // 线程入口函数
                pThInfo,            // 线程入口函数的参数
                0,                    // 立即启动线程
                &dwThreadId);        // 返回线程的id值

        //检查返回值是否创建线程成功
        if (hThread == NULL)
        {
            printf("创建线程失败。\n");
            closesocket(pThInfo->tcps);
            delete pThInfo;
        }
    }
    return 0;
}

//FTP初始化,创建一个侦听套接字:
int InitFTP(SOCKET *pListenSock)
{
    // 按照此步骤创建新的服务器端套接字，关于创建套接字的前三个步骤都相同
    // startup->socket->bind->listen
    WORD wVersionRequested; // 用于指定Winsock版本，WORD类型是一个16位的无符号整数
    WSADATA wsaData; // 用于存储Winsock版本信息
    int err; // 用于存储错误信息
    SOCKET tcps_listen; // 用于存储侦听套接字

    wVersionRequested = MAKEWORD(2, 2); // 指定Winsock版本为2.2
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        printf("Winsock初始化时发生错误!\n");
        return 0;
    }
    // 检查Winsock版本是否正确
    /**
     * LOBYTE和HIBYTE是Windows API中的宏，用于从一个16位的整数中提取高8位和低8位的值。
     */
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        WSACleanup();
        printf("无效Winsock版本!\n");
        return 0;
    }

    tcps_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcps_listen == INVALID_SOCKET)
    {
        WSACleanup();
        printf("创建Socket失败!\n");
        return 0;
    }

    SOCKADDR_IN tcpaddr;
    tcpaddr.sin_family = AF_INET;
    tcpaddr.sin_port = htons(CMD_PORT);
    tcpaddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
    err = bind(tcps_listen, (SOCKADDR *) &tcpaddr, sizeof(tcpaddr));
    if (err != 0)
    {
        err = WSAGetLastError();
        WSACleanup();
        printf("Scoket绑定时发生错误!\n");
        return 0;
    }
    err = listen(tcps_listen, 3);
    if (err != 0)
    {
        WSACleanup();
        printf("Scoket监听时发生错误!\n");
        return 0;
    }
    *pListenSock = tcps_listen;
    return 1;
}


// 建立数据连接的套接字，用于传输文件
// pDatatcps:用于存储数据连接套接字
// pClientAddr:指向客户端的控制连接套接字地址,需要使用其中的IP地址
// 返回值:0表示失败,1正常
int InitDataSocket(SOCKET *pDatatcps, SOCKADDR_IN *pClientAddr)
{
    SOCKET datatcps;

    // 创建socket
    datatcps = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (datatcps == INVALID_SOCKET)
    {
        printf("Creating data socket failed!\n");
        return 0;
    }

    SOCKADDR_IN tcpaddr;
    memcpy(&tcpaddr, pClientAddr, sizeof(SOCKADDR_IN));
    tcpaddr.sin_port = htons(DATA_PORT); // 如若有什么意外只需要在头文件修改端口值

    // 主动请求连接客户端，客户端的地址由传入的参数决定
    // 主动的目的在于客户端无法提前告知服务端数据连接的端口号，因此服务端无法主动向客户端发起连接。为了解决这个问题
    // 常见的方法是在命令连接时由客户端提供数据连接的地址和端口号，然后服务端再主动连接客户端的数据连接地址和端口号。
    if (connect(datatcps, (SOCKADDR *) &tcpaddr, sizeof(tcpaddr)) == SOCKET_ERROR)
    {
        printf("Connecting to client failed!\n");
        closesocket(datatcps);
        return 0;
    }

    *pDatatcps = datatcps;
    return 1;
}

// 处理命令报文
// tcps:控制连接套接字
// pcmd:指向待处理的命令报文
// pClientAddr:指向客户端控制连接套接字地址
// 返回值:0表示有错或者需要结束连接,1正常
/**
 * @param tcps 控制连接套接字
 * @param pCmd 指向待处理的命令报文
 * @param pClientAddr 指向客户端控制连接套接字地址
 * @return 0表示有错或者需要结束连接,1正常
 */
int ProcessCmd(SOCKET tcps, CmdPacket *pCmd, SOCKADDR_IN *pClientAddr)
{
    SOCKET datatcps;   // 数据连接套接字
    RspnsPacket rspns;  // 回复报文
    FILE *file;
    char downloadPath[256] = "../cmake-build-debug/SharingFiles/";
    char* downloadFilePath = strcat(downloadPath, pCmd->param);

    char uploadPath[256] = "../cmake-build-debug/SharingFiles/";
    char* uploadFilePath = strcat(uploadPath, pCmd->param);

    // 根据命令类型分派执行:
    switch (pCmd->cmdid)
    {
        case LS: // 列出当前目录下的文件列表
            // 首先建立数据连接:
            if (!InitDataSocket(&datatcps, pClientAddr))
            {
                return 0;
            }
            //发送文件列表信息:
            if (!SendFileList(datatcps))
            {
                return 0;
            }
            break;
        case PWD: // 展示当前目录的绝对路径
            rspns.rspnsid = OK;
            // 获取当前目录,并放至回复报文中
            if (!GetCurrentDirectory(RSPNS_TEXT_SIZE, rspns.text))
            {
                strcpy(rspns.text, "Can't get current dir!\n");
            }
            if (!SendRspns(tcps, &rspns))
            {
                return 0;
            }
            break;
        case CD: // 设置当前目录,使用win32 API 接口函数
            if (SetCurrentDirectory(pCmd->param))
            {
                rspns.rspnsid = OK;
                if (!GetCurrentDirectory(RSPNS_TEXT_SIZE, rspns.text))
                {
                    strcpy(rspns.text, "切换当前目录成功！但是不能获取到当前的文件列表！\n");
                }
            }
            else
            {
                strcpy(rspns.text, "不能更换到所选目录！\n");
            }
            if (!SendRspns(tcps, &rspns))
            {
                // 发送回复报文
                return 0;
            }
            break;
        case DOWN: // 处理下载文件请求:
            file = fopen(downloadFilePath, "rb"); // 打开要下载的文件
            if (file)
            {
                rspns.rspnsid = OK;
                // 将“下载文件pCmd->param”写入rspns.text中
                sprintf(rspns.text, "下载文件%s\n", pCmd->param);
                if (!SendRspns(tcps, &rspns)) // 如果能通过控制连接捎带，则直接捎带传送数据，否则创建额外的数据连接来传送数据
                {
                    fclose(file);
                    return 0;
                }
                else
                {
                    // 创建额外的数据连接来传送数据:
                    if (!InitDataSocket(&datatcps, pClientAddr))
                    {
                        fclose(file);
                        return 0;
                    }
                    if (!SendFile(datatcps, file))
                    {
                        return 0;
                    }
                    fclose(file);
                }
            }
            else  // 打开文件失败
            {
                rspns.rspnsid = ERR;
                strcpy(rspns.text, "不能打开文件！请重试或尝试下载其他文件\n");
                if (!SendRspns(tcps, &rspns))
                {
                    return 0;
                }
            }
            break;
        case UP: // 处理上传文件请求
            // 首先发送回复报文
            strcpy(uploadFilePath, pCmd->param);
            // 首先看一下服务器上是否已经有这个文件里，如果有就告诉客户端不用传输了
            if (FileExists(uploadFilePath))
            {
                rspns.rspnsid = ERR;
                sprintf(rspns.text, "服务器已经存在名字为%s的文件！\n", uploadFilePath);
                if (!SendRspns(tcps, &rspns))
                {
                    return 0;
                }
            }
            else
            {
                rspns.rspnsid = OK;
                if (!SendRspns(tcps, &rspns))
                {
                    return 0;
                }
                // 另建立一个数据连接来接受数据:
                if (!InitDataSocket(&datatcps, pClientAddr))
                {
                    return 0;
                }
                if (!RecvFile(datatcps, uploadFilePath))
                {
                    return 0;
                }
            }
            break;
        case QUIT:
            printf("客户端断开连接。\n");
            rspns.rspnsid = OK;
            strcpy(rspns.text, "欢迎再次访问！\n");
            SendRspns(tcps, &rspns);
            return 0;
    }
    return 1;
}

// 发送回复报文
int SendRspns(SOCKET tcps, RspnsPacket *prspns)
{
    if (send(tcps, (char *) prspns, sizeof(RspnsPacket), 0) == SOCKET_ERROR)
    {
        printf("与客户端失去连接。\n");
        return 0;
    }
    return 1;
}

// 接收命令报文
// tcps:控制连接套接字
// pCmd:用于存储返回的命令报文
// 返回值:0表示有错或者连接已经断开,1表示正常
int RecvCmd(SOCKET tcps, char *pCmd) // 用于从客户端接收命令
{
    int nRet;
    int left = sizeof(CmdPacket);

    //从控制连接中读取数据,大小为 sizeof(CmdPacket):
    while (left)
    {
        nRet = recv(tcps, pCmd, left, 0);
        if (nRet == SOCKET_ERROR)
        {
            printf("从客户端接受命令时发生未知错误！\n");
            return 0;
        }
        if (!nRet)
        {
            printf("客户端关闭了连接！\n");
            return 0;
        }
        left -= nRet;
        pCmd += nRet;
    }
    return 1;   // 成功获取命令报文
}


// 发送一项文件信息，再sendfilelist当中被调用:
int SendFileRecord(SOCKET datatcps, WIN32_FIND_DATA *pfd)
{
    char filerecord[MAX_PATH + 32];
    FILETIME ft;
    FileTimeToLocalFileTime(&pfd->ftLastWriteTime, &ft);
    SYSTEMTIME lastwtime;
    FileTimeToSystemTime(&ft, &lastwtime);
    // 如果pfd->dwFileAttributes中包含FILE_ATTRIBUTE_DIRECTORY（代表这个文件是一个目录）
    // 则dir指向字符串"<DIR>"，否则指向空字符串""。
    char *dir = (char *) (pfd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "<DIR>" : "");
    sprintf(filerecord, "%04d-%02d-%02d%02d:%02d   %5s   %10d   %-20s\n",
            lastwtime.wYear,
            lastwtime.wMonth,
            lastwtime.wDay,
            lastwtime.wHour,
            lastwtime.wMinute,
            dir,
            pfd->nFileSizeLow,
            pfd->cFileName);
    if (send(datatcps, filerecord, strlen(filerecord), 0) == SOCKET_ERROR)
    {
        printf("发送文件列表时发生未知错误！\n");
        return 0;

    }
    return 1;
}


// 发送文件列表信息
// datatcps:数据连接套接字
// 返回值:0表示出错,1表示正常
int SendFileList(SOCKET datatcps)
{
    HANDLE hff;
    WIN32_FIND_DATA fd;

    // 使用FindFirstFile函数搜索当前目录下的第一个文件
    hff = FindFirstFile("../cmake-build-debug/SharingFiles/*", &fd);
    if (hff == INVALID_HANDLE_VALUE)  // 发生错误
    {
        const char *errstr = "不能列出文件！\n";
        printf("文件列表输出失败！\n");
        if (send(datatcps, errstr, strlen(errstr), 0) == SOCKET_ERROR)
        {
            printf("发送给文件列表时发生未知错误！\n");
        }
        closesocket(datatcps);
        return 0;
    }

    // Windows API 通常使用 BOOL
    // 在使用 Windows API 中的函数时，一般使用 BOOL 类型。
    BOOL fMoreFiles = TRUE;
    while (fMoreFiles)
    {
        //发送此项文件信息:
        if (!SendFileRecord(datatcps, &fd))
        {
            closesocket(datatcps);
            return 0;
        }
        //搜索下一个文件
        fMoreFiles = FindNextFile(hff, &fd);
    }
    closesocket(datatcps);
    return 1;
}

// 通过数据连接发送文件
int SendFile(SOCKET datatcps, FILE *file)
{
    char buf[1024];
    printf("发送文件数据中=====");
    for (;;)
    {
        // 从文件中循环读取数据并发送客户端
        int r = fread(buf, 1, 1024, file);
        if (send(datatcps, buf, r, 0) == SOCKET_ERROR)
        {
            printf("×××××与客户端失去连接！\n");
            closesocket(datatcps);
            return 0;
        }
        if (r < 1024)   //文件传输结束
        {
            break;
        }
    }
    closesocket(datatcps);
    printf("=====>>完成传输!\n");
    return 1;
}

// 接收文件
// datatcps:数据连接套接字,通过它来接收数据
// filename:用于存放数据的文件名
int RecvFile(SOCKET datatcps, char *filename)
{
    char buf[1024];
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        printf("写入文件时发生未知错误！\n");
        fclose(file);
        closesocket(datatcps);
        return 0;
    }
    printf("接受文件数据中=====");
    while (1)
    {
        int r = recv(datatcps, buf, 1024, 0);
        if (r == SOCKET_ERROR)
        {
            printf("×××××从客户端接受文件时发生未知错误！\n");
            fclose(file);
            closesocket(datatcps);
            return 0;
        }
        if (!r) // 文件传输结束
        {
            break;
        }
        fwrite(buf, 1, r, file);
    }
    fclose(file);
    closesocket(datatcps);
    printf("=====>>完成传输!\n");
    return 1;
}

// 检测文件是否存在:
int FileExists(const char *filename)
{
    WIN32_FIND_DATA fd;
    if (FindFirstFile(filename, &fd) == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    return 1;
}

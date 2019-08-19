// apptester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "winsock.h"
#include "stdio.h"
#include "CfgFileParms.h"
#pragma comment (lib,"wsock32.lib")

#define MAX_BUFFER_SIZE 40000 //缓冲的最大大小

//基于select的定时器结构，目的是把数据的收发和定时都统一到一个事件驱动框架下
//可以有多个定时器，本设计实现了一个基准定时器，为周期性10ms定时，也可以当作是一种心跳计时器
//其余的定时器可以在这个基础上完成，可行的方案存在多种
//看懂设计思路后，自行扩展以满足需要
//基准定时器一开启就会立即触发一次
struct threadTimer_t {
	int iType; //为0表示周期性定时器，定时达到后，会自动启动下一次定时
	ULONG ulInterval;
	LARGE_INTEGER llStopTime;
}sBasicTimer;

struct socket_list {
	int num;
	SOCKET sock_array[64];
};

void init_list(socket_list* list)
{
	int i;
	list->num = 0;
	for (i = 0; i < 64; i++) {
		list->sock_array[i] = 0;
	}
}
void insert_list(SOCKET s, socket_list* list)
{
	int i;
	for (i = 0; i < 64; i++) {
		if (list->sock_array[i] == 0) {
			list->sock_array[i] = s;
			list->num += 1;
			break;
		}
	}
}
void delete_list(SOCKET s, socket_list* list)
{
	int i;
	for (i = 0; i < 64; i++) {
		if (list->sock_array[i] == s) {
			list->sock_array[i] = 0;
			list->num -= 1;
			break;
		}
	}
}
void make_fdlist(socket_list* list, fd_set* fd_list)
{
	int i;
	for (i = 0; i < 64; i++) {
		if (list->sock_array[i] > 0) {
			FD_SET(list->sock_array[i], fd_list);
		}
	}
}
void initTimer()
{
	sBasicTimer.iType = 0;
	sBasicTimer.ulInterval = 10 * 1000;//10ms,单位是微秒，10ms相对误差较小
	QueryPerformanceCounter(&sBasicTimer.llStopTime);
}
//根据系统当前时间设置select函数要用的超时时间——to，每次在select前使用
void setSelectTimeOut(timeval* to, struct threadTimer_t* sT)
{
	LARGE_INTEGER llCurrentTime;
	LARGE_INTEGER llFreq;
	LONGLONG next;
	//取系统当前时间
	QueryPerformanceFrequency(&llFreq);
	QueryPerformanceCounter(&llCurrentTime);
	if (llCurrentTime.QuadPart >= sT->llStopTime.QuadPart) {
		to->tv_sec = 0;
		to->tv_usec = 0;
		//		sT->llStopTime.QuadPart += llFreq.QuadPart * sT->ulInterval / 1000000;
	}
	else {
		next = sT->llStopTime.QuadPart - llCurrentTime.QuadPart;
		next = next * 1000000 / llFreq.QuadPart;
		to->tv_sec = (long)(next / 1000000);
		to->tv_usec = long(next % 1000000);
	}

}
//根据系统当前时间判断定时器sT是否超时，可每次在select后使用，返回值true表示超时，false表示没有超时
bool isTimeOut(struct threadTimer_t* sT)
{
	LARGE_INTEGER llCurrentTime;
	LARGE_INTEGER llFreq;
	//取系统当前时间
	QueryPerformanceFrequency(&llFreq);
	QueryPerformanceCounter(&llCurrentTime);

	if (llCurrentTime.QuadPart >= sT->llStopTime.QuadPart) {
		if (sT->iType == 0) {
			//定时器是周期性的，重置定时器
			sT->llStopTime.QuadPart += llFreq.QuadPart * sT->ulInterval / 1000000;
		}
		return true;
	}
	else {
		return false;
	}
}
int main(int argc, char* argv[])
{
	SOCKET s, sock;
	struct sockaddr_in ser_addr, remote_addr;
	int len;
	char *buf;
	char *sendbuf;//测试数据
	WSAData wsa;
	int retval;
	struct socket_list sock_list;
	fd_set readfds, writefds, exceptfds;
	timeval timeout;
	int i;
	unsigned long arg;
	int linecount = 0;
	int port;
	string s1,s2,s3;
	int count = 0;

	sendbuf = (char*)malloc(MAX_BUFFER_SIZE);
	buf = (char*)malloc(MAX_BUFFER_SIZE);
	if (sendbuf == NULL || buf == NULL) {
		if (sendbuf != NULL) {
			free(sendbuf);
		}
		if (buf != NULL) {
			free(buf);
		}
		cout << "内存不够"<<endl;
		return 0;
	}

	CCfgFileParms cfgParms;

	if (argc == 4) {
		s1 = argv[1];
		s2 = argv[2];
		s3 = argv[3];
	}
	else if (argc == 3) {
		s1 = argv[1];
		s2 = "APP";
		s3 = argv[2];
	}
	else {
		//从命令行读取
		cout << "请输入设备号：";
		cin >> s1;
		//cout << "请输入层次名（大写）：";
		//cin >> s2;
		s2 = "APP";
		cout << "请输入实体号：";
		cin >> s3;
	}
	WSAStartup(0x101, &wsa);
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == SOCKET_ERROR)
		return 0;

	ser_addr.sin_family = AF_INET;
	ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);

	cfgParms.setDeviceID(s1);
	cfgParms.setLayer(s2);
	cfgParms.setEntityID(s3);
	cfgParms.read();
	cfgParms.print();
	if (!cfgParms.isConfigExist) {
	//从键盘输入，需要连接的API端口号
		printf("Please input this Layer port: ");
		scanf_s("%d", &port);

		ser_addr.sin_port = htons(port);
	}
	else {
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myPort", 0);
		ser_addr.sin_port = htons(port);
	}
	bind(s,(sockaddr*)&ser_addr,sizeof(ser_addr));

	remote_addr.sin_family = AF_INET;
	remote_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); //假设物理层模拟软件在本地
	if (!cfgParms.isConfigExist) {
		//从键盘输入，需要连接的物理层模拟软件的端口号
		printf("Please input Lower Layer port: ");
		scanf_s("%d", &port);

		remote_addr.sin_port = htons(port);
	}
	else {
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"lowerPort", 0);
		remote_addr.sin_port = htons(port);
	}

	//	listen(s,5);
	timeout.tv_sec = 1; //时间间隔1秒
	timeout.tv_usec = 0;
	init_list(&sock_list);
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	//设置套接字为非阻塞态
	arg = 1;
	ioctlsocket(s,FIONBIO,&arg);
	insert_list(s, &sock_list);

	for (i = 0; i < MAX_BUFFER_SIZE; i++) {
		sendbuf[i] = 1; //初始化数据全为1,只是为了测试
	}
	/*for (count = 0; count < 10000; count++) {
		//全速发送测流量限制
		sendto(s, sendbuf, 4000, 0, (sockaddr*)& ser_addr, sizeof(ser_addr));
	}*/
	initTimer();
	while (1) {
		make_fdlist(&sock_list, &readfds);
		//采用了基于select机制，不断发送测试数据，和接收测试数据，也可以采用多线程，一线专发送，一线专接收的方案
		//设定超时时间
		setSelectTimeOut(&timeout, &sBasicTimer);
		retval = select(0, &readfds, &writefds, &exceptfds, &timeout);
		/* UDP套接字没有特别多的错误需要处理
		if (retval == SOCKET_ERROR) {
			retval = WSAGetLastError();
			break;
		}*/
		if (true == isTimeOut(&sBasicTimer)) {
			//超时
			count++;
			if (count % 10 == 0) {
				//相当于100ms发送一份测试数据
				len = 4000; //这个大小可以测试流量
				sendto(s, sendbuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
			}
		}
		for (i = 0; i < 64; i++) {
			if (sock_list.sock_array[i] == 0)
				continue;
			sock = sock_list.sock_array[i];
			if (FD_ISSET(sock, &readfds)) {
				for (i = 0; i < 8; i++) {
					buf[i] = 0; //正常情况没有必要，这里只是为了便于检查是否有正确的数据接收
				}
				retval = recv(sock, buf, MAX_BUFFER_SIZE, 0); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
				if (retval == 0) {
					closesocket(sock);
					printf("close a socket\n");
					delete_list(sock, &sock_list);
					continue;
				}
				else if (retval == -1) {
					retval = WSAGetLastError();
					if (retval == WSAEWOULDBLOCK || retval == WSAECONNRESET)
						continue;
					closesocket(sock);
					printf("close a socket\n");
					delete_list(sock, &sock_list);
					continue;
				}
				//收到数据后，打印
				printf("\n______________________________\n");
				for (i = 0; i < retval; i++) {
					linecount++;
					if (buf[i] == 0) {
						printf("0 ");
					}
					else {
						printf("1 ");
					}
					if (linecount > 40) {
						printf("\n");
						linecount = 0;
					}
				}
			}

		}
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);
	}
	free(sendbuf);
	free(buf);
	closesocket(s);
	WSACleanup();
	return 0;
}


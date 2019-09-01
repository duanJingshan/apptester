// apptester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "winsock.h"
#include "stdio.h"
#include "CfgFileParms.h"
#include <conio.h>
#pragma comment (lib,"wsock32.lib")

#define MAX_BUFFER_SIZE 5000 //缓冲的最大大小

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

void code(unsigned long x, char A[], int length)
{
	unsigned long test;
	int i;
	//高位在前
	test = 1;
	test = test << (length - 1);
	for (i = 0; i < length; i++) {
		if (test & x) {
			A[i] = 1;
		}
		else {
			A[i] = 0;
		}
		test = test >> 1; //本算法利用了移位操作和"与"计算，逐位测出x的每一位是0还是1.
	}
}
unsigned long decode(char A[], int length)
{
	unsigned long x;
	int i;

	x = 0;
	for (i = 0; i < length; i++) {
		if (A[i] == 0) {
			x = x << 1;;
		}
		else {
			x = x << 1;
			x = x | 1;
		}
	}
	return x;
}
//返回值是转出来有多少位
int ByteArrayToBitArray(char* bitA, int iBitLen, char* byteA, int iByteLen)
{
	int i;
	int len;

	len = min(iByteLen, iBitLen / 8);
	for (i = 0; i < len; i++) {
		//每次编码8位
		code(byteA[i], &(bitA[i * 8]), 8);
	}
	return len * 8;
}
//返回值是转出来有多少个字节，如果位流长度不是8位整数倍，则最后1字节不满
int BitArrayToByteArray(char* bitA, int iBitLen, char* byteA, int iByteLen)
{
	int i;
	int len;
	int retLen;

	len = min(iByteLen * 8, iBitLen);
	if (iBitLen > iByteLen * 8) {
		//截断转换
		retLen = iByteLen;
	}
	else {
		if (iBitLen % 8 != 0)
			retLen = iBitLen / 8 + 1;
		else
			retLen = iBitLen / 8;
	}

	for (i = 0; i < len; i += 8) {
		byteA[i / 8] = (char)decode(bitA + i, 8);
	}
	return retLen;
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
void print_data(char * buf,int length,int mode)
{
	int i;
	int linecount = 0;
	if (mode == 0) {
		length = BitArrayToByteArray(buf,length,buf,length);
	}
	for (i = 0; i < length; i++) {
		linecount++;
		printf("%c ", buf[i]);
		if (linecount >= 40) {
			printf("\n");
			linecount = 0;
		}
	}
	printf("\n");
	linecount = 0;
	for (i = 0; i < length; i++) {
		linecount++;
		printf("%02x ", (unsigned char)buf[i]);
		if (linecount >= 40) {
			printf("\n");
			linecount = 0;
		}
	}
	printf("\n");
}
int main(int argc, char* argv[])
{
	SOCKET sock;
	struct sockaddr_in ser_addr, remote_addr;
	int len;
	char *sendByteBuf; //字节数组测试数据
	char *sendBitBuf;//位数组试数据
	char* recvBuf; //用来接收数据
	WSAData wsa;
	int retval;
	fd_set readfds;
	timeval timeout;
	int i;
	unsigned long arg;
	int linecount = 0;
	int port;
	string s1,s2,s3; //设备号，层次号（不传），实体号
	int count = 0;
	int iWorkMode = 0;
	int iSndTotal = 0;
	int iSndTotalCount = 0;
	int iSndErrorCount = 0;
	int iRcvTotal = 0;
	int iRcvTotalCount = 0;
	int spin = 0;
	int autoSendTime = 10;
	int autoSendSize = 100;
	int lowerMode = 1;

	//带外命令接口
	SOCKET iCmdSock = 0;

	sendBitBuf = (char*)malloc(MAX_BUFFER_SIZE);
	sendByteBuf = (char*)malloc(MAX_BUFFER_SIZE);
	recvBuf = (char*)malloc(MAX_BUFFER_SIZE);
	if (sendBitBuf == NULL || sendByteBuf == NULL || recvBuf == NULL) {
		if (sendBitBuf != NULL) {
			free(sendBitBuf);
		}
		if (sendByteBuf != NULL) {
			free(sendByteBuf);
		}
		if (recvBuf != NULL) {
			free(recvBuf);
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
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == SOCKET_ERROR)
		return 0;


	cfgParms.setDeviceID(s1);
	cfgParms.setLayer(s2);
	cfgParms.setEntityID(s3);
	cfgParms.read();
	cfgParms.print();
	if (!cfgParms.isConfigExist) {
	//从键盘输入，需要连接的API端口号
		printf("Please input this Layer port: ");
		scanf_s("%d", &port);

		ser_addr.sin_family = AF_INET;
		ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		ser_addr.sin_port = htons(port);
		if (bind(sock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
			return 0;
		}

		remote_addr.sin_family = AF_INET;
		remote_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); //假设物理层模拟软件在本地

		//从键盘输入，需要连接的物理层模拟软件的端口号
		printf("Please input Lower Layer port: ");
		scanf_s("%d", &port);
		remote_addr.sin_port = htons(port);

		//从键盘输入，下层接口类型，除了物理层，默认都是1，物理层也要与模拟软件的upperMode一致
		printf("Please input Lower Layer mode: ");
		scanf_s("%d", &lowerMode);

		//从键盘输入，工作方式
		printf("Please input Working Mode: ");
		scanf_s("%d", &iWorkMode);
		if (iWorkMode / 10 == 1) {
			//自动发送
			//从键盘输入，发送间隔和发送大小
			printf("Please input send time interval（ms）: ");
			scanf_s("%d", &autoSendTime);
			printf("Please input send size（bit）: ");
			scanf_s("%d", &autoSendSize);
		}
	}
	else {
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myPort", 0);
		if (retval == -1) {
			//默认参数
			return 0;
		}
		ser_addr.sin_family = AF_INET;
		ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		ser_addr.sin_port = htons(port);
		if (bind(sock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
			//绑定错误，退出
			return 0;
		}
		remote_addr.sin_family = AF_INET;
		remote_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); //假设物理层模拟软件在本地
		retval = cfgParms.getValueInt(&port, CCfgFileParms::LOWER, (char*)"lowerPort", 0);
		if (retval == -1) {
			return 0;
		}
		remote_addr.sin_port = htons(port);

		retval = cfgParms.getValueInt(&lowerMode, CCfgFileParms::LOWER, (char*)"lowerMode", 0);
		if (retval == -1) {
			lowerMode = 1;
		}

		retval = cfgParms.getValueInt(&iWorkMode, CCfgFileParms::BASIC, (char*)"workMode", 0);
		if (retval == -1) {
			iWorkMode = 0;
		}
		if (iWorkMode / 10 == 1) {
			retval = cfgParms.getValueInt(&autoSendTime, CCfgFileParms::BASIC, (char*)"autoSendTime", 0);
			if (retval == -1) {
				autoSendTime = 10;
			}
			retval = cfgParms.getValueInt(&autoSendSize, CCfgFileParms::BASIC, (char*)"autoSendSize", 0);
			if (retval == -1) {
				autoSendSize = 800;
			}
		}
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myCmdPort", 0);
		if (retval == -1) {
			//默认参数，不接受命令
			iCmdSock = 0;
		}
		else {
			iCmdSock = socket(AF_INET, SOCK_DGRAM, 0);
			if (iCmdSock == SOCKET_ERROR)
				iCmdSock = 0;
			else {
				ser_addr.sin_family = AF_INET;
				ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
				ser_addr.sin_port = htons(port);
				if (bind(iCmdSock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
					closesocket(iCmdSock);
					iCmdSock = 0;
				}
			}
		}
	}

	//	listen(s,5);
	timeout.tv_sec = 1; //时间间隔1秒
	timeout.tv_usec = 0;
	//设置套接字为非阻塞态
	arg = 1;
	ioctlsocket(sock,FIONBIO,&arg);
	if (iCmdSock > 0) {
		arg = 1;
		ioctlsocket(iCmdSock, FIONBIO, &arg);
	}

	for (i = 0; i < MAX_BUFFER_SIZE; i++) {
		sendByteBuf[i] = 'a'; //初始化数据全为字符'a',只是为了测试
	}
	for (i = 0; i < MAX_BUFFER_SIZE; i++) {
		sendBitBuf[i] = 1; //初始化数据全为数据1,只是为了测试
	}

	initTimer();
	while (1) {
		FD_ZERO(&readfds);
		if (sock > 0) {
			FD_SET(sock, &readfds);
		}
		if (iCmdSock > 0) {
			FD_SET(iCmdSock, &readfds);
		}
		//采用了基于select机制，不断发送测试数据，和接收测试数据，也可以采用多线程，一线专发送，一线专接收的方案
		//设定超时时间
		setSelectTimeOut(&timeout, &sBasicTimer);
		retval = select(0, &readfds, NULL, NULL, &timeout);
		if (true == isTimeOut(&sBasicTimer)) {
			//超时
			count++;
			switch (iWorkMode / 10) {
				//发送|打印：发送（0，等待键盘输入；1，自动）|打印（0，定期打印统计；1，每次收发打印细节）
			case 0:
				if (_kbhit()) {
					//第一下按键，一般都是试探，读进来作废
					printf( "输入字符串：");
					fflush(stdout);
					fflush(stdin);
					scanf_s("%s", sendByteBuf, MAX_BUFFER_SIZE);
					if (lowerMode == 1) {
						retval = sendto(sock, sendByteBuf, (int)strlen(sendByteBuf) + 1, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
					}
					else {
						retval = ByteArrayToBitArray(sendBitBuf, MAX_BUFFER_SIZE, sendByteBuf, ((int)strlen(sendByteBuf) + 1));
						retval = sendto(sock, sendBitBuf, retval, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
						retval = retval ;
					}
					if (retval > 0) {
						iSndTotal += retval;
						iSndTotalCount++;
					}
					else {
						iSndErrorCount++;
					}
				}
				break;
			case 1:
				//定时发送
				if (count % autoSendTime == 0) {
					//相当于100ms发送一份测试数据
					len = autoSendSize; //这个大小可以测试流量
					if (lowerMode == 0) {
						retval = sendto(sock, sendBitBuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
					}
					else {
						retval = sendto(sock, sendByteBuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
					}
					if (retval <= 0) {
						iSndErrorCount++;
					}
					else {
						iSndTotal += retval;
						iSndTotalCount++;
					}
				}

				break;
			}
			switch(iWorkMode % 10){
			case 0:
				//仅定期打印 500ms
				if (count % 50 == 0) {
					spin++;
					switch (spin) {
					case 1:
						printf("\r-");
						break;
					case 2:
						printf("\r\\");
						break;
					case 3:
						printf("\r|");
						break;
					case 4:
						printf("\r/");
						spin = 0;
						break;
					}
					if (lowerMode == 1) {
						cout << "共发送 " << iSndTotal << " 字节," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
						cout << " 共接收 " << iRcvTotal << " 字节," << iRcvTotalCount << " 次";
					}
					else {
						cout << "共发送 " << iSndTotal << " 位," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
						cout << " 共接收 " << iRcvTotal << " 位," << iRcvTotalCount << " 次";
					}
				}
				break;
			}
		}
		if (FD_ISSET(sock, &readfds)) {
			for (i = 0; i < 8; i++) {
				recvBuf[i] = 0; //正常情况没有必要，这里只是为了便于检查是否有正确的数据接收
			}
			retval = recv(sock, recvBuf, MAX_BUFFER_SIZE, 0); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
			//处理不正常的接收
			if (retval == 0) {
				closesocket(sock);
				sock = 0;
				printf("close a socket\n");
				continue;
			}
			else if (retval == -1) {
				retval = WSAGetLastError();
				if (retval == WSAEWOULDBLOCK || retval == WSAECONNRESET)
					continue;
				closesocket(sock);
				sock = 0;
				printf("close a socket\n");
				continue;
			}
			iRcvTotal += retval;
			iRcvTotalCount++;
			switch (iWorkMode % 10) {
			case 1:
				if (lowerMode == 1) {
					//打印数据
					//收到数据后，打印
					cout << endl;
					cout << "共发送 " << iSndTotal << " 字节," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
					cout << " 共接收 " << iRcvTotal << " 字节," << iRcvTotalCount << " 次" << endl;
				}
				else {
					cout << endl;
					cout << "共发送 " << iSndTotal << " 位," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
					cout << " 共接收 " << iRcvTotal << " 位," << iRcvTotalCount << " 次" << endl;

				}
				break;
			case 0:
				break;
			}
			print_data(recvBuf, retval,lowerMode);
		}
		if (iCmdSock == 0)
			continue;
		if (FD_ISSET(iCmdSock, &readfds)) {
			retval = recv(iCmdSock, recvBuf, MAX_BUFFER_SIZE, 0); 
			if (retval <= 0) {
				continue;
			}
			if (strncmp(recvBuf, "exit", 5) == 0) {
				break;
			}
		}
	}
	free(sendBitBuf);
	free(sendByteBuf);
	free(recvBuf);
	if(sock > 0)
		closesocket(sock);
	if (iCmdSock)
		closesocket(iCmdSock);
	WSACleanup();
	return 0;
}


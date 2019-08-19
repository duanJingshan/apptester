// readfile.cpp : Defines the entry point for the console application.
//

#include <string>
#include <fstream>
#include <iostream>
#include <winsock.h>
#pragma comment(lib,"wsock32.lib")
//#include <iostream>
using namespace std;


class CCfgFileParms
{
public:


	bool isConfigExist; //false，读取错误，true，正确读到内容
	enum parms_set{BASIC,LOWER,UPPER,PEER};

	int getValueInt(int* dst, parms_set type, char* name, int index); //type 哪个大组的参数，name 参数名字，index，该大组的哪个小组——因为每个小组内的参数都是同名的
	char* getValueStr(parms_set type, char* name, int index);//type 哪个大组的参数，name 参数名字，index，该大组的哪个小组——因为每个小组内的参数都是同名的
	int getNumber(parms_set type); //取得参数组的数量，例如共享信道上有多个对端地址等，在网络层，可设置多个邻居路由器信息等

	void print(); //打印读出来的配置数据
	int read();   //根据设定的devieceID，layer和entityID来读取配置文件，读完后检查isConfigExist标志，为true表示正确读到内容
	void setDeviceID(string ID)
	{
		deviceID = ID;
	}
	void setDeviceID(int ID)
	{
		char buf[40];
		_itoa_s(ID,buf,40,10);
		deviceID = buf;
	}
	void setLayer(string lay)
	{
		layer = lay;
	}

	void setEntityID(string ID)
	{
		entityID = ID;
	}
	void setEntityID(int ID)
	{
		char buf[40];
		_itoa_s(ID, buf, 40, 10);
		entityID = buf;
	}


	CCfgFileParms();
	CCfgFileParms(string devID,string layerID,string entID);

	~CCfgFileParms();
protected:
	string deviceID;
	string layer;
	string entityID;
	struct parmEntry {
		char* name;
		char* cValue;
		int value;
	};
	struct cfgParms {
		int number; //有多少组参数，例如共享信道上，存在多个对等的物理层实体，
		int parms;  //每组参数有多少项，例如共享信道上，每个对等实体的参数至少有两项：IP地址和端口号
		parmEntry* entryArray;
	};
	struct cfgParms basic; //基本参数组是所有不属于与高层、低层和对等有关的参数，一般只有1组
	struct cfgParms lower; //低层实体参数组，一般是接口地址，如果有多个低层实体对应，可能有多组参数
	struct cfgParms upper; //高层实体参数组，一般是接口地址，如果有多个高层实体对应，可能有多组参数
	struct cfgParms peer;  //对等实体参数组，一般是对等实体的地址等信息，如果有多个对等层实体对应，可能有多组参数

	void myStrcpy(char * str,string src );
	
	void printArray(parms_set type);
	int readArray(parms_set type, ifstream & cfgFile);

	void freeArray(parms_set type);
	void * getCfgParms(parms_set type);
	int getAndCheckLine(ifstream& cfgFile, string& str);

};




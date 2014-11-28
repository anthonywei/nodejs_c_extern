#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <stdint.h>
#include <stdlib.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <string.h>

#include <node.h>
#include <v8.h>
#include <node_buffer.h>

using namespace node;
using namespace v8;
using namespace std;

#define INDEX_SHMEM_LEFT	0
#define INDEX_SHMEM_RIGHT	1

#define ID_SHMEM_HEAD 60001
#define ID_SHMEM_LEFT 60002
#define ID_SHMEM_RIGHT 60003


#pragma pack (1)

class CMemHead
{
public:
	unsigned int version;	//版本号
	unsigned int time;		//时间戳
	unsigned int index;		//对应的数据是哪一块LEFT 还是 RIGHT
	unsigned int imax;		//最大记录的条数
	unsigned int reserved2;
};

//单条记录
class CMemUnit
{
public:
	unsigned int version;	//版本号
	unsigned int time;		//时间戳
	unsigned int port;		//端口
	unsigned int hash;		//hash的值
	char szAppName[20];
	char szIpList[1024];
};

#pragma pack ()


class MyShm
{
private:
	private:
	uint32_t m_dwShmSize; //大小
	char*	 m_pHead;	//头指针
	uint32_t m_dwShmKey;
	bool	 m_Init;
	int		 m_dwShmId;
	char	 m_ErrMsg[256];
public:
	MyShm()
	{
		m_Init = false;
		m_dwShmSize = 0;
		m_pHead = 0;
		m_dwShmKey = 0;
		m_dwShmId = 0;
		m_ErrMsg[0] = '\0';
	}
	
	~MyShm()
	{
		m_Init = false;
		m_dwShmSize = 0;
		m_pHead = 0;
		m_dwShmId = 0;
		m_dwShmKey = 0;
	}
	
	char* GetShm(int iKey, int iSize, int iFlag, char * szErrMsg = NULL)
	{
		int iShmID;
		char* sShm;

		if ((iShmID = shmget(iKey, iSize, iFlag)) < 0) {
			if (szErrMsg)
				snprintf(szErrMsg, 64, "shmget %d %d:%s", iKey, iSize, strerror(errno));
			return NULL;
		}
		
		if ((sShm = (char *) shmat(iShmID, NULL, 0)) == (char *) -1) {
			if (szErrMsg)
				snprintf(szErrMsg, 64, "shmat %d %d:%s", iKey, iSize, strerror(errno));
			return NULL;
		}
		return sShm;
	}

	bool DtShm(char * pShmAddr, char * szErrMsg)
	{
		assert(pShmAddr);
		
		if (shmdt(pShmAddr) >= 0)
			return true;
		else
		{
			if (szErrMsg)
				snprintf(szErrMsg, 64, "shmdt %p:%s", pShmAddr, strerror(errno));
			return false;
		}
	}

		
	int	InitShm(int iShmKey, int iSize, void *& pMem, int iFlag)
	{	
		//已经初始化，且是同一块共享内存，直接返回ok
		if(m_Init == true && iShmKey == (int)m_dwShmKey)
		{
			pMem = m_pHead;
			return 0;
		}

		//先以非创建的模式尝试
		m_dwShmId = shmget(iShmKey, iSize, iFlag & (~IPC_CREAT));
		if(m_dwShmId < 0)
		{
			m_dwShmId = shmget(iShmKey, iSize, iFlag | IPC_CREAT);
			if(m_dwShmId < 0)
			{
				snprintf(m_ErrMsg, 64, "shmget with create failed %d %d:%s", 
					iShmKey, iSize, strerror(errno));
				
				printf("shmget failed %d %d:%s\n", 
					iShmKey, iSize, strerror(errno));
			}

		}


		m_pHead = (char*)shmat(m_dwShmId, NULL, 0);
		if(m_pHead <= 0)
		{
			snprintf(m_ErrMsg, 64, "shmat  failed %d %d:%s", 
				iShmKey, iSize, strerror(errno));
			
			printf("shmat failed %d %d:%s\n", 
					iShmKey, iSize, strerror(errno));
		}

		
		pMem = m_pHead;

		m_dwShmKey = iShmKey;
		m_dwShmSize = iSize;

		m_Init = true;

		//printf("Shm Key:%u Id:%d Size:%u\n", m_dwShmKey, m_dwShmId, m_dwShmSize);
		
		return 0;
	}

	bool DetachShm()
	{	
		if(m_pHead != NULL)
		{
			shmdt(m_pHead);
		}

		return true;
	}

	void ClearShm()
	{
		if(m_dwShmId)
			shmctl(m_dwShmId, IPC_RMID, NULL);

	}
};



uint32_t hashpjw(const char *psKey, uint32_t ulKeyLength)
{		
	uint32_t h = 0, g;
	const char *pEnd = psKey + ulKeyLength;
	while(psKey < pEnd)
	{	
		h = (h << 4) + *psKey++;
		if ((g = (h & 0xF0000000)))
		{
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}
	return h;
}	 


int BSearch(CMemUnit& oUnit, std::string strName)
{
	MyShm m_oHead;
	void* m_pHead;

	MyShm m_oLeft;
	void* m_pLeft;

	MyShm m_oRight;
	void* m_pRight;


	if(m_oHead.InitShm(ID_SHMEM_HEAD, sizeof(CMemHead), m_pHead, 0666) != 0)
	{
		return -1;
	}

	if(m_oLeft.InitShm(ID_SHMEM_LEFT, sizeof(CMemUnit) * 10000, m_pLeft, 0666) != 0)
	{
		return -1;
	}

	if(m_oRight.InitShm(ID_SHMEM_RIGHT, sizeof(CMemUnit) * 10000, m_pRight, 0666) != 0)
	{
		return -1;
	}
	
	unsigned int inputHash = hashpjw(strName.c_str(), strName.length());
	
	CMemHead *pHead = (CMemHead*)m_pHead;
	
	unsigned int index = pHead->index;
	if(index == INDEX_SHMEM_LEFT)
	{
		CMemUnit* pUnit = (CMemUnit*)m_pLeft;

		int left = 0, right = pHead->imax, mid;
		mid = ( left + right ) / 2;
		
		while( left <= right && pUnit[mid].hash!= inputHash)
		{
			if(pUnit[mid].hash < inputHash )
				left = mid + 1;
			else if(pUnit[mid].hash > inputHash )
				right = mid - 1;
			
			mid = ( left + right ) / 2;
		}

		if(pUnit[mid].hash == inputHash)
		{
			oUnit.hash = pUnit[mid].hash;
			oUnit.port = pUnit[mid].port;
			strncpy(oUnit.szAppName, pUnit[mid].szAppName, 20-1);
			strncpy(oUnit.szIpList, pUnit[mid].szIpList, 1024-1);
			
		}
		else
		{
			return -1;
		}

	}
	else
	{
		CMemUnit* pUnit = (CMemUnit*)m_pRight;
		
		int left = 0, right = pHead->imax, mid;
		mid = ( left + right ) / 2;
		
		while( left <= right && pUnit[mid].hash!= inputHash)
		{
			if(pUnit[mid].hash < inputHash )
				left = mid + 1;
			else if(pUnit[mid].hash > inputHash )
				right = mid - 1;
			
			mid = ( left + right ) / 2;
		}

		if(pUnit[mid].hash == inputHash)
		{
			oUnit.hash = pUnit[mid].hash;
			oUnit.port = pUnit[mid].port;
			strncpy(oUnit.szAppName, pUnit[mid].szAppName, 20-1);
			strncpy(oUnit.szIpList, pUnit[mid].szIpList, 1024-1);

		}
		else
		{
			return -1;
		}

		
	}

	return 0;
}

inline int SplitStringIntoVector(const char * sContent, 
											const char * sDivider, 
											std::vector<std::string> &vecStr)
{
	char * sNewContent = new char [strlen(sContent)+1];
	snprintf(sNewContent,strlen(sContent)+1,"%s",sContent);

	char * pStart = sNewContent;
	char * pEnd   = sNewContent;

	std::string strContent;

	pEnd = strstr(sNewContent,sDivider);
	if(pEnd == NULL && strlen(sNewContent)>0){
		strContent = pStart; //get the last one;
		vecStr.push_back(strContent);
	}
	

	while(pEnd){
		*pEnd = '\0';
		strContent = pStart;
		vecStr.push_back(strContent);
		
		pStart = pEnd+strlen(sDivider);
		if((*pStart) == '\0'){
			break;
		}
		
		pEnd = strstr(pStart,sDivider);
		
		if(pEnd == NULL){
			strContent = pStart; //get the last one;
			vecStr.push_back(strContent);
		}
		
	}

	delete [] sNewContent;

	return vecStr.size();
}



static Handle<Value> cc_getConfigValueByKey(const Arguments &args)
{
	HandleScope scope;
	
	if (args.Length() < 1)
	{
		ThrowException(Exception::TypeError(String::New("Wrong tye of arguments")));
		return scope.Close(Undefined());
	}


	String::Utf8Value str(args[0]);

	char* key = *str; //Buffer::Data(data->ToObject());

	if(!key)
	{
		ThrowException(Exception::TypeError(String::New("NULLLLL")));
		return scope.Close(Undefined());		
	}

	CMemUnit oUnit;
	std::string strValue = "";
	if(BSearch(oUnit, key) != 0)
	{
		strValue = "";
	}
	else
	{
		strValue = oUnit.szIpList;
	}

	return scope.Close(String::New((const char*)strValue.c_str(), strValue.length()));
}


static Handle<Value> cc_getConfigPortByKey(const Arguments &args)
{
	HandleScope scope;
	
	if (args.Length() < 1)
	{
		ThrowException(Exception::TypeError(String::New("Wrong tye of arguments, string required!")));
		return scope.Close(Undefined());
	}

	String::Utf8Value str(args[0]);

	char* key = *str; //Buffer::Data(args[0]->ToObject());
	
	CMemUnit oUnit;
	unsigned int port = 0;
	if(BSearch(oUnit, key) != 0)
	{
		port =  0;
	}
	else
	{

		port = oUnit.port;
	}

	Local<Number> port_ = Number::New(port);

	return scope.Close(port_);
}

static Handle<Value> cc_getConfigValueByKeySet(const Arguments &args)
{
	HandleScope scope;
	
	if (args.Length() < 2 || !args[1]->IsNumber())
	{
		ThrowException(Exception::TypeError(String::New("Wrong tye of arguments, string required!")));
		return scope.Close(Undefined());
	}

	String::Utf8Value str(args[0]);
	
	char* key = *str;

	int route = args[1]->Uint32Value();

	CMemUnit oUnit;
	std::string strValueSet = "";
	if(BSearch(oUnit, key) != 0)
	{
		strValueSet = "";
	}
	else
	{

		std::vector<std::string> vecIp;
		SplitStringIntoVector(oUnit.szIpList, ";", vecIp);

		//取模
		strValueSet = vecIp[route%vecIp.size()];
	}

	return scope.Close(String::New((const char*)strValueSet.c_str(), strValueSet.length()));
}


void init(Handle<Object> target) {
  NODE_SET_METHOD(target, "cc_getConfigValueByKey", cc_getConfigValueByKey);
  NODE_SET_METHOD(target, "cc_getConfigPortByKey", cc_getConfigPortByKey);
  NODE_SET_METHOD(target, "cc_getConfigValueByKeySet", cc_getConfigValueByKeySet);
  
  
}


NODE_MODULE(ConfigCenter, init);




#ifndef SQL_CONNECTION_POOL_
#define SQL_CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool {
public:
    MYSQL *GetConnection();				     //获取数据库连接
	bool ReleaseConnection(MYSQL *conn);     //释放当前使用的连接
	int GetFreeConn();					     //获取当前空闲连接数量
	void DestroyPool();					     //销毁所有连接
	//单例模式创建数据库连接池
	static connection_pool *GetInstance();
    // 初始化数据库
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;            // 最大连接数
	int m_CurConn;            // 当前已使用的连接数
	int m_FreeConn;           // 当前空闲的连接数
	locker lock;              // 互斥锁
	list<MYSQL *> connList;   // 连接池
	sem reserve;

public:
	string m_url;			 // 主机地址
	string m_Port;		     // 数据库端口号
	string m_User;		     // 登陆数据库用户名
	string m_PassWord;	     // 登陆数据库密码
	string m_DatabaseName;   // 使用数据库名
	int m_close_log;	     // 日志开关
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放：
// 通过connectionRAII类改变外部MYSQL指针使其指向连接池的连接
// 析构函数归还连接入池
class connectionRAII{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};
#endif
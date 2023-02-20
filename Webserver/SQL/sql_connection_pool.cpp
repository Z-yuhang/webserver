#include "sql_connection_pool.h"

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

// 单例模式
connection_pool *connection_pool::GetInstance() {
	static connection_pool connPool;
	return &connPool;
}

// 数据库初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++) {
		MYSQL *con = NULL;
		// 初始化连接
		con = mysql_init(con);
		if (con == NULL) {
			cout << "mysql init error" << endl;
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 建立一个到mysql数据库的连接
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
		if (con == NULL) {
			cout << "create mysql connect error" << endl;			
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;
	}
	reserve = sem(m_FreeConn);        // 信号量
	m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection() {
	MYSQL *con = NULL;
	if (0 == connList.size())
		return NULL;
	reserve.wait();                  //取出连接，信号量原子减1，为0则等待
	lock.lock();
	con = connList.front();
	connList.pop_front();
	--m_FreeConn;
	++m_CurConn;
	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
	if (NULL == con)
		return false;
	lock.lock();
	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;
	lock.unlock();
	reserve.post();                // 释放连接，信号量原子加1
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool() {
	lock.lock();
	if (connList.size() > 0) {
		list<MYSQL*>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it) {
			MYSQL *con = *it;
			mysql_close(con);           // 关闭连接
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn() {
	return this->m_FreeConn;
}

connection_pool::~connection_pool() {
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
	poolRAII->ReleaseConnection(conRAII);
}


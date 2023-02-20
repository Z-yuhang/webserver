#include "./config/config.h"

using namespace std;

int main(int argc, char *argv[]) {
    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    // 数据库用户名密码
    string user = "root";
    string passwd = "123456";
    string databasename = "webserver";

    // 创建WebServer
    WebServer server;
    // 初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num, config.close_log, config.actor_model);
    // 初始化日志系统
    server.log_write();
    //数据库
    server.sql_pool();
    // 创建线程池
    server.creatThreadpool();
    // 设置epoll的触发模式 
    server.trig_mode();
    // 开启epoll监听
    server.eventListen();
    // 主线程运行
    server.eventLoop();

    return 0;
}
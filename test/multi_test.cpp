#include <common.h>
#include <bse.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <string>
#include <thread>
#include <thread_pool.hpp>

using namespace std;
using namespace BSE;
using namespace VWNet;

bool login_test(int user,const string &pwd)
{
  int client_fd=socket(AF_INET,SOCK_STREAM,0);
  if(client_fd == -1)
  {
    printf("socket error: %s\n",strerror(errno));
    return false;
  }
  struct sockaddr_in server;
  bzero(&server,sizeof(server));
  server.sin_family=AF_INET;
  server.sin_addr.s_addr=inet_addr("127.0.0.1");
  server.sin_port=htons(8000);
  if(connect(client_fd,(struct sockaddr *)&server,sizeof(server))==-1)
  {
    printf("connect fails: %s\n",strerror(errno));
    return false;
  }

  decltype(static_cast<LoginRequest*>(nullptr)->ClientCode) client_code = user;
  string password = pwd;
  string timestamp = "10:11:22";

  LoginRequest login_req;
  constexpr auto login_req_len = sizeof(login_req);
  memset(&login_req,0,login_req_len);
  constexpr decltype(login_req.Type) msg_type = BSEMsgType::FeedLogin;
  login_req.Type = FormatData(msg_type);
  login_req.ClientCode = FormatData(client_code);
  strncpy(login_req.Password,password.c_str(),sizeof(login_req.Password));
  strncpy(login_req.Timestamp,timestamp.c_str(),sizeof(login_req.Timestamp));

  char read_data[128];
  memset(read_data,0,sizeof(read_data));

  int write_b = write(client_fd,(char*)&login_req,sizeof(login_req));
  if(write_b<0) printf("fd=%d: write error=%s\n",client_fd,strerror(errno));
  
  printf("fd=%d: write bytes=%d\n",client_fd,write_b);
  int read_b = read(client_fd,read_data,sizeof(read_data));

  if(read_b >= (int)sizeof(LoginReply))
  {
    auto reply = reinterpret_cast<LoginReply*>(read_data);
    printf("fd=%d: login reply: client=%u,flag=%d\n",
           client_fd,FormatData(reply->ClientCode),FormatData(reply->SuccessFlag));
  }
  else
  {
    printf("fd=%d: read less bytes=%d\n",client_fd,read_b);
  }
  //std::this_thread::sleep_for(std::chrono::seconds(5));
  close(client_fd);
  return true;
}

void multiple_login()
{
  thread th1(login_test,100,"pwd100");

  thread th2(login_test,100,"pwd100");

  thread th3(login_test,200,"pwd200");

  thread th4(login_test,200,"pwd200");
//    std::this_thread::sleep_for(std::chrono::seconds(1));
  thread th5(login_test,1000,"pwd1000");
  th1.join();
  th2.join();
  th3.join();
  th4.join();
  th5.join();
}

class run_task
{
public:
  void run()
  {
    th_pool_.Start();
    for(int i=0;i<6;i++)
    {
      auto task = std::bind(&run_task::print_index,this,i);
      th_pool_.Commit(std::move(task));
    }
    ::getchar();
    th_pool_.Stop();
  }
  
private:
  void print_index(int i)
  {
    login_test(i,"pwd");
    printf("Run task it:%d\n",i);
  }
private:
  ThreadPool th_pool_{4};
};

void test_thread_pool()
{
  run_task task;
  std::thread runner(&run_task::run,&task);
  runner.join();
}

int main(int argc,char**argv)
{
  multiple_login();
  //test_thread_pool();

  return 0;
}

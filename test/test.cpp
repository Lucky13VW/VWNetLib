#include <iostream>
#include <chrono>
#include <future>
#include <string.h>
#include "thread_pool.hpp"
#include "epoll_net.h"

/*
  async(std::launch::async | std::launch::deferred, f, args...)
  std::future<int> future = std::async(std::launch::async, [](){ 
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 8;  
    }); 
 
    std::cout << "waiting...\n";
    std::future_status status;
    do {
        status = future.wait_for(std::chrono::seconds(1));
        if (status == std::future_status::deferred) {
            std::cout << "deferred\n";
        } else if (status == std::future_status::timeout) {
            std::cout << "timeout\n";
        } else if (status == std::future_status::ready) {
            std::cout << "ready!\n";
        }
    } while (status != std::future_status::ready); 
 
    std::cout << "result is " << future.get() << '\n';
*/

void DoAJob(std::vector<int> vec)
{
  for(auto v: vec)
    std::cout<<v<<std::endl;
}

void DoBJob()
{
  std::cout<<"do b job"<<std::endl;
}

void test_thread_pool()
{
  std::cout<<"start thread pool"<<std::endl;
  VWNet::ThreadPool pool(5);
  std::vector<int> vec{1,2,3,4,5,9};

  pool.Commit(std::bind(DoAJob,vec));
  pool.Start();
  for(int i=0;i<10;i++) pool.Commit(DoBJob);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  pool.Stop();
  std::cout<<"pool stopped"<<std::endl;
}

class TestServer: public VWNet::EpollNetTcpServer
{
private:
  virtual size_t process_data(int fd,char *data,size_t size) override;
};

size_t TestServer::process_data(int fd,char *data,size_t size)
{
  static int count = 0;
  if(count > 2) return size;
  if(size>1)
  {
    printf("s=>%s\n",data);
    char buf[40];
    memset(buf,0,sizeof(buf));
    sprintf(buf,"%d: Server side ack=%zd",count,size);
    send_data(fd,buf,sizeof(buf));
    count++;
  }
  return size;
}

//////////////////

class TestClient: public VWNet::EpollNetTcpClient
{
  
private:
  virtual void on_connect(int fd) override;
  virtual size_t process_data(int fd,char *data,size_t size) override;
};

void TestClient::on_connect(int fd)
{
  add_event(fd,EPOLLOUT);
  char buf[40];
  memset(buf,0,sizeof(buf));
  sprintf(buf,"hello server");
  send_data(fd,buf,sizeof(buf));
}

size_t TestClient::process_data(int fd,char *data,size_t size)
{
  static int count = 0;
  if(count > 2) return size;
  if(size>1)
  {
    printf("c=>%s\n",data);
    char buf[40];
    memset(buf,0,sizeof(buf));
    sprintf(buf,"%d: Client side ack:%zd",count,size);
    send_data(fd,buf,sizeof(buf));
    count++;
  }
  return size;
}

TestServer server;

void test_epoll_server()
{  
  server.Init(8000);
  server.Run();
}

TestClient client;
void test_epoll_client()
{
  
  client.Init("127.0.0.1",8000);
  client.Run();
}

int main(int argc,char *argv[])
{
  auto fut = std::async(std::launch::async,test_epoll_server);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::future_status status = fut.wait_for(std::chrono::milliseconds(200));
  
  if(status == std::future_status::deferred) std::cout<<"launch deferred"<<std::endl;
  else if(status == std::future_status::ready) std::cout<<"fut ready"<<std::endl;
  else std::cout<<"fut not ready"<<std::endl;
  
  auto fut2 = std::async(std::launch::async,test_epoll_client);
  std::future_status status2 = fut2.wait_for(std::chrono::milliseconds(200));
  if(status2 == std::future_status::deferred) std::cout<<"launch deferred"<<std::endl;
  
  std::cout<<"enter [quit] to exit the program!"<<std::endl;
  std::string cmd;
  while(true)
  {
    std::getline(std::cin,cmd);
    if(cmd == "quit")
    {
      server.Stop();
      client.Stop();
      break;
    }
    else
    {
      server.Command(7);
      client.Command(8);
    }
  }
  return 0;
}

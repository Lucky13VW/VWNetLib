#pragma once

#include <string>
#include <set>
#include <epoll_net.h>

namespace VWNet
{
  struct client_account
  {
    int code;
    std::string name;
    std::string pwd;
    std::string ts;
    std::string jnl_file;
    std::vector<int> query_msgs;
  };

  using cl_ac_ptr = std::shared_ptr<client_account>;

  struct client_config
  {
    std::string ip{"127.0.0.1"};
    int port{8000};
    int threads{2};
    int buf_size{8192};
    std::vector<cl_ac_ptr> accounts;
  };

  class BSEClient : public EpollNetTcpClient
  {
    using super = EpollNetTcpClient;
  public:
    BSEClient(size_t buf_n=BUF_SIZE,uint16_t thread_n=2) noexcept
      :super(buf_n,thread_n)
    {}
    virtual ~BSEClient(){ close_jnl_file();}
    void handle_quit() noexcept;
    void set_account(const cl_ac_ptr& account) { account_ = account;}

  protected:
    virtual void on_connect(int fd) override;
    virtual void on_disconnect(int fd) override { SockList_.erase(fd); close_jnl_file();}
    virtual void on_error(int fd,int) override { SockList_.erase(fd); close_jnl_file(); }
    
  private:
    virtual size_t process_data(int fd,char *data,size_t size) override;  
    size_t save_jnl(int fd,char *data,size_t size) noexcept;
    void close_jnl_file() noexcept;
    void send_logoff(int fd) noexcept;
    void send_login(int fd) noexcept;

  private:
    //std::string file_jnl_{"./bse_test.jnl"};
    cl_ac_ptr account_;
    FILE *jnl_file_{nullptr};
    std::set<int> SockList_;
  };
}

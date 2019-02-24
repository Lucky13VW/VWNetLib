#pragma once

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>
#include <unordered_map>
#include <queue>
#include "thread_pool.hpp"

/*
typedef union epoll_data {
  void    *ptr;
  int      fd;
  uint32_t u32;
  uint64_t u64;
} epoll_data_t;

struct epoll_event {
  uint32_t     events;    
  epoll_data_t data;      
};
*/

namespace VWNet
{
  enum class ProtType
  {
    UDP = SOCK_DGRAM,
    TCP = SOCK_STREAM
  };

  enum
  {
    CMD_QUIT = 1, // quit server
    CMD_PLAYBACK = 2, // playback jnl 
    CMD_LIST = 3,  // list clients
    CMD_KICKOFF = 4 // kick a clilent
  };

  constexpr uint32_t BUF_SIZE = 1024*8;
  constexpr uint16_t MAX_EVENTS = 32;
  constexpr uint16_t MAX_CMD_SIZE = 16;
  constexpr char CMD_DELIMITER = '|';
    
  // write request
  struct w_req
  {
    w_req() noexcept{}
    ~w_req() { if(in_buf!=nullptr) free(in_buf); }

    char *in_buf{nullptr};
    char *data{nullptr};
    size_t size{0};  
  };

  // net context for one network relationship
  struct net_context
  {
    net_context(size_t size,const sockaddr_in& rt) noexcept
    :rt_saddr(rt)
    {
      r_data=r_buf=(char*)malloc(size);
    }
    ~net_context() { if(r_buf!=nullptr) free(r_buf); }
    char *r_buf{nullptr};
    char *r_data{r_buf};
    size_t r_size{0};
    std::mutex r_mutex;
    std::mutex w_mutex;
    std::mutex w_req_mutex;
    std::queue<std::unique_ptr<w_req>> w_req_q;
    sockaddr_in rt_saddr;
  };

  class EpollNet
  {
  public:
    EpollNet(size_t buf_n, uint16_t thread_n,ProtType type,int mode) noexcept
      :Epollfd_(0),
       SvrSfd_(0),
       Cmdfd_(0),
       BufSize_(buf_n),
       Type_(type),
       Mode_(mode),
       ThreadPool_(thread_n),
       InitDone_(false){}
    
    virtual ~EpollNet()
    {
      if(Epollfd_>0) close(Epollfd_);
      if(SvrSfd_>0) close(SvrSfd_);
    };
    
    bool Init(int port) noexcept {return Init("",port); }
    bool Init(const std::string &ip_addr,int port) noexcept;
    void Run();
    void Command(int cmd,std::string para="") noexcept;
    void Stop() noexcept { Command(CMD_QUIT); }
    void Set(size_t buf_size,uint16_t thread_num) noexcept
    {
      BufSize_ = buf_size;
      ThreadPool_.SetThreadNum(thread_num);
    }
    
  protected:
    using ContextPtr = std::shared_ptr<net_context>;
    using FdHandler = std::function<void(int)>;
    using CmdHandler = std::function<void(std::string para)>;

    bool make_nonblock(int sfd) noexcept;
    void add_event(int fd,int event) noexcept { set_event(fd,EPOLL_CTL_ADD,event); }
    void modify_event(int fd,int event) noexcept { set_event(fd,EPOLL_CTL_MOD,event); }
    void delete_event(int fd,int event) noexcept { set_event(fd,EPOLL_CTL_DEL,event); }
    void create_context(int fd,const sockaddr_in& remote);
    void delete_context(int fd=-1);
    void clear_context_event(int fd,int index);
    EpollNet::ContextPtr get_context(int fd);
    
    template<typename T>
    void registry_fd_handler(int fd,T &&fun)
    {
      make_nonblock(fd);
      add_event(fd,EPOLLIN);
      FdHandlers_.emplace(fd,std::forward<T>(fun));
    }
    
    template<typename T>
    void registry_command_handler(int cmd,T &&fun)
    {
      CmdHandlers_.emplace(cmd,std::forward<T>(fun));
    }

    void unregistry_fd_handler(int fd)
    {
      delete_event(fd,EPOLLIN);
      FdHandlers_.erase(fd);
    }

    template<typename T>
    void start_async_task(T &&fun)
    {
      ThreadPool_.Commit(std::forward<T>(fun));
    }

    virtual bool start_service() = 0;
    virtual void on_error(int fd,int err) {}
    // not all system supports EPOLLRDHUP
    virtual void on_epoll_rdhup(int fd) {} 

  private:
    bool setup_epoll() noexcept;
    void clear_epoll() noexcept;
    void set_event(int fd,int opt,int event) noexcept;
    //void handle_events(size_t index);
    bool handle_command(int fd);
    int setup_command_fd() noexcept;
    void clear_command_fd(int fd) noexcept;
    // build-in command 'quit'
    //void add_cmd_quit();

    virtual bool do_bind() noexcept { return true; }
    virtual void add_monitor() {}
    virtual void add_command() {} 
    virtual void handle_read(int fd,size_t index) = 0;
    virtual void handle_write(int fd,size_t index) = 0;
     
  protected:
    int Epollfd_;
    int SvrSfd_;
    int Cmdfd_;
    size_t BufSize_;
    sockaddr_in SvrAddr_;
    epoll_event Events_[MAX_EVENTS];  
    
  private:
    ProtType Type_;
    int Mode_;
    ThreadPool ThreadPool_;
    bool InitDone_;
    std::mutex ContextMutex_;
    std::unordered_map<int,ContextPtr> NetContext_;
    std::unordered_map<int,FdHandler> FdHandlers_;

    std::unordered_map<int,CmdHandler> CmdHandlers_;
    std::string FifoCmdName_;  
  };

  class EpollNetTcp : public EpollNet
  {
  public:
    EpollNetTcp(size_t buf_n,uint16_t thread_n) noexcept
      :EpollNet(buf_n,thread_n,ProtType::TCP,EPOLLET)
    {}
    virtual ~EpollNetTcp() = default;

  protected:
    // send write request to current fd and return immediately
    // by default function will copy data to its own build-in buffer
    size_t send_data(int fd,char *data,size_t size,bool copy_data=true);
    void disconnect(int fd) noexcept { shutdown(fd,SHUT_RDWR); } 
    virtual void on_connect(int fd) {}
    virtual void on_disconnect(int fd) {}
    virtual void on_epoll_rdhup(int fd) override { on_disconnect(fd); }

  private:
    virtual void handle_read(int fd,size_t index) override;
    virtual void handle_write(int fd,size_t index) override;
    // Return value: bytes processed by this function. 
    // Handle_read will buffer unprocessed remaining bytes(maximum BUF_SIZE).
    // But remaining bytes won't be sent to this function unless it reads more bytes.
    // Recommend to process as much as possible or buffer data inside process_data
    // which knows business logic well.
    virtual size_t process_data(int fd,char *data,size_t size) = 0;

  };
  
  // epoll ET mode TCP server
  class EpollNetTcpServer : public EpollNetTcp
  {
  public:
    EpollNetTcpServer(size_t buf_n=BUF_SIZE,uint16_t thread_n=std::thread::hardware_concurrency()) noexcept
      :EpollNetTcp(buf_n,thread_n)
    {}
    virtual ~EpollNetTcpServer() = default;
  protected:
    virtual bool start_service() override;
  private:
    virtual bool do_bind() noexcept override;
    void handle_accept(int fd);
  };

  // epoll ET mode client
  class EpollNetTcpClient : public EpollNetTcp
  {
  public:
    EpollNetTcpClient(size_t buf_n=BUF_SIZE,uint16_t thread_n=std::thread::hardware_concurrency()) noexcept
      :EpollNetTcp(buf_n,thread_n)
    {}
    virtual ~EpollNetTcpClient() = default;
  protected:
    virtual bool start_service() override;
  };

}

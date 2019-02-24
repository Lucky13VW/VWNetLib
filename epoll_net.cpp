#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <iostream>     
#include <utility>
#include <random>
#include "epoll_net.h"

namespace VWNet
{
  template<typename T, typename... Args>
  std::unique_ptr<T> make_unique(Args&&... args)
  {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }

  /*
  void EpollNet::registry_stdin_fd()
  {
    int fd = STDIN_FILENO;
    make_nonblock(fd);
    add_event(fd,EPOLLIN);
    printf("Enter \'q\' to quit!\n");
  }
  
  void EpollNet::add_cmd_quit()
  {
    auto handle = std::bind([](std::promise<bool>&pro){ pro.set_value(false);},std::placeholders::_1);
    registry_command_handler(0,std::move(handle));
  }
  */

  void EpollNet::create_context(int fd,const sockaddr_in& remote)
  {
    std::lock_guard<std::mutex> guard(ContextMutex_);
    auto context = std::make_shared<net_context>(BufSize_,remote);
    NetContext_.emplace(fd,context);
  }

  void EpollNet::delete_context(int fd)
  {
    std::lock_guard<std::mutex> guard(ContextMutex_);
    if(fd == -1)
    {
      for(auto &it:NetContext_)
      {
        shutdown(it.first,SHUT_RDWR);
        close(it.first);
      }
      NetContext_.clear();
    }
    else
    {
      auto it = get_context(fd);
      if(it == nullptr) return;
      
      shutdown(fd,SHUT_RDWR);
      close(fd);
      NetContext_.erase(fd);
    }
  }

  EpollNet::ContextPtr EpollNet::get_context(int fd)
  {
    auto it = NetContext_.find(fd);
    if (it == NetContext_.end())
    {
      printf("fd=%d get_context: failed to find fd\n",fd);
      return nullptr;
    }
    return it->second; 
  }

  void EpollNet::set_event(int fd,int opt,int event) noexcept
  {
    epoll_event ep_ev;
    ep_ev.data.fd = fd;
    ep_ev.events = event | Mode_ | EPOLLRDHUP;
    if(epoll_ctl(Epollfd_,opt,fd,&ep_ev) == -1)
      printf("fd=%d epoll_ctl: opt=%d,ev=%d,error=%s\n",fd,opt,event,strerror(errno));
  }

  bool EpollNet::setup_epoll() noexcept
  {
    if(Epollfd_ >0) close(Epollfd_);
    Epollfd_ = epoll_create(1);

    if(Epollfd_ == -1)
    {
      printf("epoll_create: errno:%s\n",strerror(errno));
      return false;
    }
    return true;
  }

  void EpollNet::clear_epoll() noexcept
  {
    if(Epollfd_ >0)
    {
      close(Epollfd_);
      Epollfd_ = 0;
    }
  }

  bool EpollNet::make_nonblock(int sfd) noexcept
  {
    int flags=0;
    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1)
    {
      printf("fcntl getfl,errno:%s\n",strerror(errno));
      return false;
    }

    flags |= O_NONBLOCK;
    if (fcntl (sfd, F_SETFL, flags) == -1)
    {
      printf("fcntl setfl,errno:%s\n",strerror(errno));
      return false;
    }
    return true;
  }

  bool EpollNet::Init(const std::string &ip_addr,int port) noexcept
  {
    if(SvrSfd_>0) close(SvrSfd_);
    SvrSfd_ = socket(AF_INET, static_cast<int>(Type_), 0);
    
    memset(&SvrAddr_, 0, sizeof(SvrAddr_));
    SvrAddr_.sin_family = AF_INET;
    if(ip_addr == "") SvrAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    else SvrAddr_.sin_addr.s_addr = inet_addr(ip_addr.c_str());
    SvrAddr_.sin_port = htons(port);

    int opt = 1;
    if ( setsockopt(SvrSfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
      printf("setsockopt: errno=%s\n",strerror(errno));
      InitDone_ = false;
      return false;
    }

    InitDone_ = do_bind();

    return InitDone_;
  }

  void EpollNet::Run()
  {
    if(!InitDone_)
    {
      printf("Intial not done!\n");
      return;
    }

    if(!setup_epoll()) return;
    if(!start_service()) return;

    int cmd_fd = setup_command_fd();
    //add_cmd_quit(); // build in command: quit
    add_command(); // commands supported in handle_command
    add_monitor(); // add extra fd into epoll_wait

    printf("starting thread pool: count=%u,buf_size=%zd\n",ThreadPool_.ThreadNum(),BufSize_);
    ThreadPool_.Start();
    
    bool is_running = true;
    while(is_running)
    {
      int event_count = epoll_wait(Epollfd_,Events_,MAX_EVENTS,-1);
      if(event_count <= 0)
      {
        printf("epoll_wait: errno=%s\n",strerror(errno));
        break;
      }
      for(int i=0; i<event_count; i++)
      {
        int fd = Events_[i].data.fd;
        if(cmd_fd == fd) // command control 
        {
          is_running = handle_command(fd);
          if(!is_running) break;
          else continue;
        }

        if(Events_[i].events & EPOLLRDHUP)
        {
          // remote close
          printf("fd=%d remote close\n",fd);
          on_epoll_rdhup(fd);
          clear_context_event(fd,i);
          continue;
        }  
        else if(Events_[i].events & EPOLLERR || Events_[i].events & EPOLLHUP )//|| !(Events_[i].events & EPOLLIN))
        {
          // An error has occured on this fd, or the socket is not ready for reading 
          printf("fd=%d I/O broken, event(%d)\n",fd,Events_[i].events);
          on_error(fd,errno);
          clear_context_event(fd,i);
          continue;
        }

        // process specified fd handler, e.g: svr fd for accept,file fd for monitor.
        auto it = FdHandlers_.find(fd);
        if(it != FdHandlers_.end()) ThreadPool_.Commit(it->second,fd); 
        else 
        {
          // process read & write
          if(Events_[i].events & EPOLLIN) ThreadPool_.Commit(&EpollNet::handle_read,this,fd,i); 
          if(Events_[i].events & EPOLLOUT) ThreadPool_.Commit(&EpollNet::handle_write,this,fd,i);
        }
      }
    }

    ThreadPool_.Stop();
    clear_command_fd(cmd_fd);
    delete_context();
    clear_epoll();
  }

  /*
    auto task = std::bind(it->second,fd);
    ThreadPool_.Commit(std::move(task));
    auto task = std::bind(&EpollNet::handle_read,this,fd,i);
    ThreadPool_.Commit(std::move(task));
  */

  /*
  void EpollNet::handle_events(size_t index)
  {
    int fd = Events_[index].data.fd;

    if (Events_[index].events & EPOLLIN) { handle_read(fd,index); }
    if (Events_[index].events & EPOLLOUT){ handle_write(fd,index); }
  }
  */

  void EpollNet::clear_context_event(int fd,int index)
  {
    auto it = get_context(fd);
    if(it == nullptr) return; // clear already done in other thread
    
    delete_event(fd,Events_[index].events);
    delete_context(fd);
  }

  int EpollNet::setup_command_fd() noexcept
  {
    std::random_device rd;
    FifoCmdName_ = "fifocmd_" + std::to_string(rd());
    if(access(FifoCmdName_.c_str(), F_OK) == 0) unlink(FifoCmdName_.c_str());
        
    if( mkfifo(FifoCmdName_.c_str(), 0777) == -1) //O_CREAT|O_EXCL|0755) == -1)
    {
      printf("mkfifo error:%s\n",strerror(errno));
      return -1;
    }
    
    int fd = open(FifoCmdName_.c_str(),O_RDONLY|O_NONBLOCK);
    if(fd == -1)
    {
      printf("pipe open for read:%s\n",strerror(errno));
      unlink(FifoCmdName_.c_str());
      return -2;
    }
    else
    {
      // blocking mode open
      Cmdfd_ = open(FifoCmdName_.c_str(),O_WRONLY);//|O_NONBLOCK);
      if(Cmdfd_ == -1)
      {
        printf("pipe open for write:%s\n",strerror(errno));
        close(fd);
        unlink(FifoCmdName_.c_str());
        return -3;
      }
    }
    //make_nonblock(fd);
    add_event(fd,EPOLLIN);
    return fd;
  }

  void EpollNet::Command(int cmd, std::string para) noexcept
  {
    char cmd_buf[MAX_CMD_SIZE];
    memset(cmd_buf,0,sizeof(cmd_buf));
    if(para != "")
    {
      snprintf(cmd_buf,sizeof(cmd_buf),"%d%c%s",
               cmd,CMD_DELIMITER,para.c_str());
    }
    else
    {
      snprintf(cmd_buf,sizeof(cmd_buf),"%d",cmd);
    }
    int ret = write(Cmdfd_,cmd_buf,sizeof(cmd_buf));
    if(ret<=0)
    {
      printf("command error:%s\n",strerror(errno));
    }
  }
  
  bool EpollNet::handle_command(int fd)
  {
    bool is_running = true;
    char cmd_buf[MAX_CMD_SIZE];
    memset(cmd_buf,0,sizeof(cmd_buf));
    int ret = read(fd,cmd_buf,sizeof(cmd_buf));
    if(ret <= 0 && errno != EAGAIN)
    {
      printf("handle_command read error:%s\n",strerror(errno));
      return is_running;
    }
    std::string cmd_line = cmd_buf;
    // split command and parameter
    std::string input_cmd;
    std::string input_para="";
    auto split = cmd_line.find(CMD_DELIMITER);
    if(std::string::npos != split && cmd_line.size()>split+1)
    {
      input_cmd = cmd_line.substr(0,split);
      input_para = cmd_line.substr(split+1);
    }
    else input_cmd = cmd_line;
    
    int cmd = atoi(input_cmd.c_str());

    if(cmd == CMD_QUIT) is_running = false;
    else
    {
      // run command handler in seperate thread
      auto it = CmdHandlers_.find(cmd);
      if(it != CmdHandlers_.end()) ThreadPool_.Commit(it->second,input_para);
      else printf("unsupport command:%s\n",cmd_line.c_str());      
    }
    return is_running;
  }

  //auto task = std::bind(it->second,input_para);
  //ThreadPool_.Commit(std::move(task));  
  // but wait for the return value from promise
  /*
    std::promise<bool> prom;
    auto futu = prom.get_future();
    std::thread exec_cmd(it->second, std::ref(prom));
    
    is_running = futu.get();
    exec_cmd.detach();
  */

  void EpollNet::clear_command_fd(int fd) noexcept
  {
    if(Cmdfd_>0) close(Cmdfd_);
    if(fd>0)
    {
      close(fd);
      unlink(FifoCmdName_.c_str());
    }
  }

  // ********* EpollNetTcp ***********
  void EpollNetTcp::handle_read(int fd,size_t index)
  {
    auto ctxt = get_context(fd);
    if(ctxt == nullptr) return;

    std::lock_guard<std::mutex> guard(ctxt->r_mutex);
    /*  === r_buf structure ===
      r_buf   r_data                       BUF_SIZE
      |        |                              |
      [........(...r_size...)...size_remain...]  
     */
    size_t size_remain = BufSize_ - (ctxt->r_data - ctxt->r_buf + ctxt->r_size);
    while(true)
    {
      if(size_remain == 0)
      {        
        if(ctxt->r_data > ctxt->r_buf)
        {
          // no remaining, move it to begining
          memmove(ctxt->r_buf,ctxt->r_data,ctxt->r_size);
          ctxt->r_data = ctxt->r_buf;
          size_remain = BufSize_ - ctxt->r_size;
        }
        else
        {
          printf("fd=%d can't read, no read buffer remaining! \n",fd);
          break;
        }
      }
      
      int byte_r = read(fd,ctxt->r_data+ctxt->r_size,size_remain);
      if (byte_r == -1)
      {
        // If errno == EAGAIN, that means we have read all data.
        if(errno== EAGAIN || errno == EWOULDBLOCK) break;
        else
        {
          printf("fd=%d handle_read: error %s\n",fd,strerror(errno));
          on_error(fd,errno);
          clear_context_event(fd,index);
          break;
        }
      }
      else if (byte_r == 0)
      {
        printf("fd=%d handle_read:remote close.\n",fd);
        on_disconnect(fd);
        clear_context_event(fd,index);
        break;
      }
      else
      {
        ctxt->r_size += byte_r;
        size_remain -= byte_r;
        ssize_t byte_p = process_data(fd,ctxt->r_data,ctxt->r_size);
        // if nothing processed, continue to read more
        if(byte_p == 0) continue; 

        // remaing part
        ctxt->r_size -= byte_p;
        if(ctxt->r_size == 0)
        {
          // all buffer data processed
          ctxt->r_data = ctxt->r_buf;
          size_remain = BufSize_;
        }else ctxt->r_data += byte_p;
      }
    }
  }

  void EpollNetTcp::handle_write(int fd,size_t index)
  {     
    auto ctxt = get_context(fd);
    if(ctxt == nullptr) return;

    std::lock_guard<std::mutex> guard(ctxt->w_mutex);
     
    while(!ctxt->w_req_q.empty())
    {
      auto &w_req = ctxt->w_req_q.front();          
      int w_byte = write(fd,w_req->data,w_req->size);
      if (w_byte == -1)
      {
        if(errno == EAGAIN || errno == EWOULDBLOCK) break;
        else
        {
          printf("fd=%d handle_write: error %s\n",fd,strerror(errno));
          on_error(fd,errno);
          clear_context_event(fd,index);
          break;
        }
      }
      else if(w_byte == 0)
      {
        printf("fd=%d handle_write: remote close.\n",fd);
        on_disconnect(fd);
        clear_context_event(fd,index);
        break;
      }
      else
      {
        w_req->size -= w_byte;
        if(w_req->size == 0) 
        {
          // current request is done, pop it from request queue
          std::lock_guard<std::mutex> guard(ctxt->w_req_mutex);
          ctxt->w_req_q.pop(); 
        }
        else w_req->data+=w_byte;// process the remaining
      }
    }
  }

  size_t EpollNetTcp::send_data(int fd,char *data,size_t size,bool copy_data)
  {
    auto ctxt = get_context(fd);
    if(ctxt == nullptr) return 0;
    
    auto new_w_req = make_unique<w_req>();
    size_t req_size = size;
    if(copy_data)
    {
      // copy data to request build-in buffer
      //if(size > BufSize_) req_size = BufSize_; // max copy BufSize_
      new_w_req->in_buf = (char*)malloc(req_size);
      if(new_w_req->in_buf == nullptr)
      {
        req_size = BufSize_; // resize to BufSize_ and try again
        new_w_req->in_buf = (char*)malloc(req_size);
        if(new_w_req->in_buf == nullptr)
        {
          printf("fd=%d send_data can't malloc buffer for size=%zd\n",fd,req_size);
          return 0;
        }
      }
      memcpy(new_w_req->in_buf,data,req_size);
      new_w_req->data = new_w_req->in_buf;
      new_w_req->size = req_size;
    }
    else
    {
      // the caller will keep the data
      new_w_req->data = data;
      new_w_req->size = size;
    }

    {
      std::lock_guard<std::mutex> guard(ctxt->w_req_mutex);
      ctxt->w_req_q.emplace(std::move(new_w_req));
    }
    
    // add write request
    modify_event(fd,EPOLLIN|EPOLLOUT);
  
    return req_size;
  }

  // *********  EpollNetTcpServer *********
  bool EpollNetTcpServer::do_bind() noexcept
  {
    if(bind(SvrSfd_, (const sockaddr*)&SvrAddr_, sizeof(SvrAddr_)) == -1)
    {
      printf("bind error:%s\n",strerror(errno));
      return false;
    } 
    make_nonblock(SvrSfd_);
    return true;
  }

  bool EpollNetTcpServer::start_service()
  {
    if(listen(SvrSfd_,SOMAXCONN)!=0)
    {
      printf("listen error,errno=%s\n",strerror(errno));
      return false;
    }
    else
    {
      printf("fd=%d listen on %u\n",SvrSfd_,ntohs(SvrAddr_.sin_port));
      // registry accept handler
      auto handler = std::bind(&EpollNetTcpServer::handle_accept,this,std::placeholders::_1);
      registry_fd_handler(SvrSfd_,std::move(handler));
      return true;
    }
  }

  void EpollNetTcpServer::handle_accept(int fd)
  {
    while(true)
    {
      sockaddr_in cli_addr;
      socklen_t  cliaddr_len = sizeof(cli_addr);
      int conn_fd = accept(fd,(sockaddr*)&cli_addr,&cliaddr_len);
      if (conn_fd == -1)
      {
        // Since listen_fd is non blocking, EAGIN means no more connections
        // all accept has been done
        if (errno != EAGAIN && errno != EWOULDBLOCK)
          printf("fd=%d accept errno:%s\n",fd,strerror(errno));
        break;
      } 
      else
      {
        printf("fd=%d connected: %s:%u\n",conn_fd,
                 inet_ntoa(cli_addr.sin_addr),ntohs(cli_addr.sin_port));
        // make it non blocking,and start to monitor the conn_fd
        create_context(conn_fd,cli_addr);
        on_connect(conn_fd);
        make_nonblock(conn_fd);
        add_event(conn_fd,EPOLLIN); // wait for incoming message
      }
    }
  }

  // *********  EpollNetTcpClient *********
  
  bool EpollNetTcpClient::start_service()
  {
    int status = connect(SvrSfd_, (sockaddr*)&SvrAddr_, sizeof(SvrAddr_)) != 0;
    if(status == 0)
    {
      printf("fd=%d connected %s:%u\n",SvrSfd_,inet_ntoa(SvrAddr_.sin_addr),ntohs(SvrAddr_.sin_port));
      make_nonblock(SvrSfd_);
      create_context(SvrSfd_,SvrAddr_);
      add_event(SvrSfd_,EPOLLIN); // wait for incoming message
      on_connect(SvrSfd_);
      return true;
    }
    else //if (EINPROGRESS != errno) 
    {
      printf("connect failed:%s\n",strerror(errno));
      return false;
    }
  }
}

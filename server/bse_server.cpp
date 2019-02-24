#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h> 
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <atomic>
#include <common.h>
#include <json/reader.h>
#include <json/value.h>
#include "bse_server.h"

static std::atomic<bool> is_quit_current_task{false};

namespace VWNet
{
  constexpr size_t map_size_limit = 1024*1024*128;
  
  bse_client_context::bse_client_context(const std::string &file) noexcept
  :pb_file(file)
  {
    if(file.size()==0) return; // no need to monitor file
    
    notify_fd = inotify_init();
    if (notify_fd < 0)
    {
      printf("inotify_init failed:%s\n",strerror(notify_fd));
      notify_fd = 0;
    }
    else
    {
      watch_fd = inotify_add_watch(notify_fd, file.c_str(), IN_CLOSE_WRITE);
      if (watch_fd < 0)
      {
        printf("inotify_add_watch errno=%s \n", strerror(errno));
        close(notify_fd);
        notify_fd = 0;
        watch_fd = 0;
      }
    }
  }

  bse_client_context::~bse_client_context()
  {
    if(watch_fd>0) inotify_rm_watch(notify_fd,watch_fd);
    if(notify_fd>0) close(notify_fd);
  }

  bool BSESimulator::start_service()
  {
    if(KeepAliveSetting_.enable)
    {
      int keep_alive = 1; // enable keep alive 
      int keep_idle = KeepAliveSetting_.idle; // check after idle
      int keep_interval = KeepAliveSetting_.interval; // wait time between checking
      int keep_count = KeepAliveSetting_.count; // check times
      
      printf("set keep alive:idle=%u,intvl=%u,cnt=%u\n",keep_idle,keep_interval,keep_count);
      if(setsockopt(SvrSfd_, SOL_SOCKET, SO_KEEPALIVE, (void *)&keep_alive, sizeof(keep_alive)) == -1)
        printf("set keep alive: errno=%s\n",strerror(errno));
 
      if(setsockopt(SvrSfd_, SOL_TCP, TCP_KEEPIDLE, (void*)&keep_idle, sizeof(keep_idle)) == -1)
        printf("set keep alive: errno=%s\n",strerror(errno));
 
      if(setsockopt(SvrSfd_, SOL_TCP, TCP_KEEPINTVL, (void *)&keep_interval, sizeof(keep_interval)) == -1)
        printf("set keep alive: errno=%s\n",strerror(errno));
 
      if(setsockopt(SvrSfd_, SOL_TCP, TCP_KEEPCNT, (void *)&keep_count, sizeof(keep_count)) == -1)
        printf("set keep alive: errno=%s\n",strerror(errno));

    }
    return super::start_service();
  }

  std::pair<char*,size_t> BSESimulator::map_file(const std::string &file_path) noexcept
  {
    std::pair<char*,size_t> result{nullptr,0};
    
    const char *file_name = file_path.c_str();
    struct stat f_info;
    int fd = stat(file_name,&f_info);
    if(fd == -1)
    {
      printf("file stat error:%s\n",strerror(errno));
      return std::move(result);
    }    
    size_t f_size = f_info.st_size;

    if(f_size > map_size_limit)
    {
      printf("file oversize: file=%s, limit=%zd\n",file_name,map_size_limit);
      return std::move(result);
    }
    fd = open(file_name,O_RDONLY);
    if(fd == -1 )
    {
      printf("file open failed:%s\n",strerror(errno));
      return std::move(result);
    }
    // MAP_SHARED
    char *p_map = (char *)mmap(NULL,f_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    if(p_map == MAP_FAILED)
    {
      printf("file map failed:%s\n",strerror(errno));
      return std::move(result);
    }
    result.first = p_map;
    result.second = f_size;
    return std::move(result);
  }

  void BSESimulator::unmap_file(char *p_map,size_t m_size) noexcept
  {
    if(p_map != nullptr && m_size>0) munmap(p_map,m_size);
  }

  void BSESimulator::send_for_account(const CliContextPtr &cli_one_account) noexcept
  {
    const auto &file_name = cli_one_account->pb_file; 
    auto result = map_file(file_name);
    auto p_map = result.first;
    auto f_size = result.second;
    if(p_map == nullptr) return;
    
    // send to all fd in this account
    auto fd_set = cli_one_account->clients_fd;
    for(auto c_fd:fd_set)
    {
      printf("Playback: fd=%d,file=%s,size=%zd\n",c_fd,file_name.c_str(),f_size);
      send_data_all(c_fd,p_map,f_size);
    }
    unmap_file(p_map,f_size);
  }

  void BSESimulator::send_query_file(int fd,const std::string &query_file) noexcept
  {
    auto result = map_file(query_file);
    auto p_map = result.first;
    auto f_size = result.second;
    if(p_map != nullptr)
    {
      printf("SendQuery: fd=%d,file=%s,size=%zd\n",fd,query_file.c_str(),f_size);
      send_data_all(fd,p_map,f_size);
   
      unmap_file(p_map,f_size);
    }
  }

  size_t BSESimulator::send_data_all(SockFd fd,char *data,size_t size) noexcept
  {
    size_t done_n = 0;
    while(done_n < size)
    {
      ssize_t sent_n = send_data(fd,data+done_n,size-done_n);
      if(sent_n<=0) break;
      else
      {
        done_n += sent_n;
        printf("Sending... %u%%\r",int(done_n*100/size));
      }
    }
    printf("\n%s",cmd_prompt.c_str());
    fflush(stdout);
    return done_n;
  }

  void BSESimulator::send_jnl_file(const CliContextPtr &cli_one_account) noexcept
  {
    const auto &file_path = cli_one_account->jnl_file;
    const char *file_name = file_path.c_str();
    struct stat f_info;
    int fd = stat(file_name,&f_info);
    if(fd == -1)
    {
      printf("file stat:file=%s,errno=%s\n",file_name,strerror(errno));
      return;
    }    
    size_t f_size = f_info.st_size;

    fd = open(file_name,O_RDONLY);
    if(fd == -1 )
    {
      printf("file open:file=%s,err=%s\n",file_name,strerror(errno));
      return;
    }

    is_quit_current_task = false;
    
    auto jnl_rate = cli_one_account->jnl_rate;
    constexpr size_t dft_buf_size = 1024*1024*4;
    size_t jnl_buf_size = jnl_rate>0 ? jnl_rate:dft_buf_size;
      
    char *p_jnl_buf = (char*)malloc(jnl_buf_size);
    size_t process_size = 0;
    ssize_t r_size = 0;
    ssize_t offset = 0;
    constexpr uint16_t slice_per_sec = 200;
    constexpr uint16_t sleep_per_cycle = 990/slice_per_sec;
    size_t rate_size = jnl_rate>0 ? jnl_buf_size/slice_per_sec : jnl_buf_size;
    printf("Playback: file=%s,size=%zd(bytes),rate=%d(bytes/s), please Standby...\n",file_name,f_size,jnl_rate);

    auto senddata = [this](int c_fd,char *p_buf,size_t send_rate_size){  
      size_t done_n = 0;
      while(done_n < send_rate_size)
      {
        ssize_t sent_n = send_data(c_fd,p_buf+done_n,send_rate_size-done_n);
        if(sent_n<=0) break;
        else done_n += sent_n;
      }
      return done_n;
    };

    auto begin_time = std::chrono::steady_clock::now();
    int cur_pct = 0,pre_pct = 0;
    while(process_size < f_size)
    {
      offset = lseek(fd,process_size,SEEK_SET);
      r_size = read(fd,p_jnl_buf,jnl_buf_size);
      if(r_size < 0)
      {
        printf("Playback error: file=%s,error=%s\n",file_name,strerror(errno));
        break;
      }
      else if(r_size == 0)
      {
        printf("Playback: read done,file_size=%zd,offset=%zd\n",f_size,offset);
        break;
      }

      process_size += r_size;
      // send to all fd in this account
      auto c_fds = cli_one_account->clients_fd;
      if(c_fds.size() == 0)
      {
        printf("About playback task...\n");
        is_quit_current_task = true;
        break;
      }
      
      for(auto c_fd:c_fds)
      {
        // rate control
        ssize_t sent_size=0;
        size_t remain_size=0;
        size_t send_rate_size=0;
        while(sent_size < r_size)
        {
          remain_size = r_size-sent_size;
          send_rate_size = remain_size>rate_size ? rate_size:remain_size;
          sent_size += senddata(c_fd,p_jnl_buf+sent_size,send_rate_size);          

          if(jnl_rate>0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_per_cycle));
        }

        pre_pct = cur_pct;
        cur_pct = int(process_size*100/f_size);
        if(cur_pct-pre_pct>0)
        {
          printf("Sending... fd=%d: %u%% %zd/%zd\r",c_fd,cur_pct,process_size,f_size);
          fflush(stdout);
          //printf("Playback: fd=%d,file=%s,%zd/%zd\n",c_fd,file_name,process_size,f_size);
        }
      }
      
      if(is_quit_current_task)
      {
        printf("\nCancelling playback task...\n");
        break;
      }
    }

    auto end_time = std::chrono::steady_clock::now();    
    auto d_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - begin_time);

    printf("\nPlayback: file=%s,%zd/%zd(bytes),time=%zd(seconds) done.\n",file_name,process_size,f_size,d_time.count());

    free(p_jnl_buf);
    close(fd);
  }

  void BSESimulator::add_command()
  {
    auto cmd_handle_pb = std::bind(&BSESimulator::command_playback,this,std::placeholders::_1);
    registry_command_handler(CMD_PLAYBACK,std::move(cmd_handle_pb));

    auto cmd_handle_list = std::bind(&BSESimulator::command_list,this,std::placeholders::_1);
    registry_command_handler(CMD_LIST,std::move(cmd_handle_list));

    auto cmd_handle_kick = std::bind(&BSESimulator::command_kick,this,std::placeholders::_1);
    registry_command_handler(CMD_KICKOFF,cmd_handle_kick);
  }

  void BSESimulator::command_list(std::string para)
  {
    printf("Listing clients online...\n");
    if(SockLookup_.size()==0)
    {
      printf("No clients online!\n");
      return;
    }
    
    // construct online code:fds groups
    std::unordered_map<AcCode,std::set<SockFd>> onlines;
    for(auto it = SockLookup_.begin(); it != SockLookup_.end();it++)
    {
      auto fd = it->first;
      auto code = it->second;
      auto on = onlines.find(code);
      if(on == onlines.end())
      {
        std::set<SockFd> fds{fd};
        onlines.emplace(code,fds);
      }
      else on->second.emplace(fd);
    }

    for(auto it = onlines.begin();it != onlines.end();it++)
    {
      auto code = it->first;
      auto account = Accounts_.find(code);
      auto name = account!=Accounts_.end()? account->second->name:"";
      printf("[Name=%s:Code=%d]\n",name.c_str(),code);
      char status = code ==-1? '*':'~';
      for(auto c_fd: it->second)
      {
        auto ctxt = get_context(c_fd);
        if(ctxt != nullptr)
        {
          printf("%cfd=%d %s:%u \n",status,c_fd,
                 inet_ntoa(ctxt->rt_saddr.sin_addr),ntohs(ctxt->rt_saddr.sin_port));
        }
        else printf("%cfd=%d\n",status,c_fd);
      }
    }
  }

  void BSESimulator::command_kick(std::string para)
  {
    if(para.size() == 0)
    {
      printf("No fd specified\n");
      return;
    }
    
    int c_fd = atoi(para.c_str());
    
    auto it = SockLookup_.find(c_fd);
    if(it == SockLookup_.end())
    {
      printf("fd=%d not found\n",c_fd);
      return;
    }
    else
    {
      printf("Kicking fd=%d from code=%d\n",c_fd,it->second);
      disconnect(c_fd);
    }
  }

  void BSESimulator::command_playback(std::string para)
  {
    if(ClientContext_.size() == 0)
    {
      printf("No connected clients!\n");
      return;
    }

    int target_code = para.size()==0 ? -1:atoi(para.c_str()); 
    for(auto it = ClientContext_.begin();it != ClientContext_.end();it++)
    {
      auto code = it->first;
      if((target_code == -1 || target_code == code) && it->second->jnl_file.size()>0)
      {
        printf("Playback for client Code=%u\n",code);
        send_jnl_file(it->second);
        if(is_quit_current_task) break;
      }
    }
  }

  size_t BSESimulator::calc_msg_size(PHMsgType type) noexcept
  {
    size_t msg_size = 0;
    switch(type)
    {
      case BSEMsgType::FeedLogin:
        msg_size = sizeof(LoginRequest);
        break;
      case BSEMsgType::FeedLogoff:
        msg_size = sizeof(LogoffRequest);
        break;
      default:
        msg_size = sizeof(QueryRequest);
        break;
    }
    return msg_size;
  }

  size_t BSESimulator::process_data(int fd,char *data,size_t size)
  {
    size_t size_total = 0;
    size_t size_remain = size;
    while(size_remain > 0)
    {
      size_t size_process = 0;
      
      if(size_remain >= sizeof(PHMsgType))
      {
        PHMsgType msg_type;
        memcpy(&msg_type,data,sizeof(PHMsgType));
        msg_type = FormatData(msg_type);
        
        size_t size_expect = calc_msg_size(msg_type);    
        if(size_remain >= size_expect)
        {
          size_process = size_expect;
          switch(msg_type)
          {
            case BSEMsgType::FeedLogin:
              handle_login(fd,reinterpret_cast<LoginRequest*>(data));
              break;
            case BSEMsgType::FeedLogoff:
              handle_logoff(fd,reinterpret_cast<LogoffRequest*>(data));
              break;
            default:
              handle_request(fd,reinterpret_cast<QueryRequest*>(data));
              break;
          }
        }
      }
      if(size_process == 0) break;
      else
      {
        size_total += size_process;
        size_remain -= size_process;
        data += size_process;
      }
    }
    return size_total;
  }

  void BSESimulator::handle_login(int fd,LoginRequest *request)
  {
    auto code = FormatData(request->ClientCode);
    std::string pwd;
    pwd.append(request->Password,strnlen(request->Password,sizeof(request->Password)));
    trim(pwd);
    std::string ts;
    ts.append(request->Timestamp,sizeof(request->Timestamp));
    printf("fd=%d Login request: code=%u,pwd=%s,time=%s\n",
           fd,code,pwd.c_str(),ts.c_str());

    auto account = Accounts_.find(code);
    uint16_t login_flag = 0;    
    if(account == Accounts_.end())
    {
      printf("fd=%d Login: code=%d not found\n",fd,code);
      login_flag = -2;
    }
    else if(account->second->pwd != pwd)
    {
      printf("fd=%d Login: code=%d wrong password\n",fd,code);
      login_flag = -2;   
    }
    else
    {
      // prepare client 
      printf("fd=%d Login accept: code=%d \n",fd,code);
      create_client_context(fd,account->second);
    }
    
    LoginReply reply;
    constexpr PHSizeType reply_len = sizeof(reply);
    constexpr PHSizeType msg_size = reply_len-sizeof(PHSizeType);
    constexpr decltype(reply.Header.Type) msg_type = BSEMsgType::FeedLogin;
    
    memset(&reply,0,reply_len);
    reply.Header.Size = FormatData(msg_size);
    reply.Header.Type = FormatData(msg_type);
    reply.ClientCode = FormatData(code);
    reply.SuccessFlag = FormatData(login_flag);
    send_data(fd,(char*)&reply,reply_len);
  }

  void BSESimulator::handle_logoff(int fd,LogoffRequest *request)
  {
    auto code = FormatData(request->ClientCode);
    printf("fd=%d Logoff: code=%u\n",fd,code);

    LogoffReply reply;
    constexpr PHSizeType reply_len = sizeof(reply);
    constexpr PHSizeType msg_size = reply_len - sizeof(PHSizeType);
    constexpr decltype(reply.Header.Type) msg_type = BSEMsgType::FeedLogoff;
    
    memset(&reply,0,reply_len);
    reply.Header.Size = FormatData(msg_size);
    reply.Header.Type = FormatData(msg_type);
    reply.ClientCode = FormatData(code);
    reply.SuccessFlag = FormatData(uint16_t(0));
    send_data(fd,(char*)&reply,reply_len);
  }
  
  void BSESimulator::handle_request(int fd,QueryRequest *request)
  {
    auto msg_type = FormatData(request->Type);
    printf("fd=%d handle_request: msg=%u\n",fd,msg_type);

    // find query file for this msg type
    std::string query_file;
    auto lookup = SockLookup_.find(fd);
    if(lookup != SockLookup_.end())
    {
      auto code = lookup->second;
      auto it =  ClientContext_.find(code);
      if(it != ClientContext_.end() && it->second->type==ac_type::query)
      {
        auto q_file_map = it->second->q_files;
        auto query = q_file_map.find(msg_type);
        if(query != q_file_map.end())
        {
          query_file = query->second;
        }
      }
    }
    
    int req_msg = msg_type;
    int reply_val = 0;

    QueryReply q_reply;
    constexpr PHSizeType q_reply_len = sizeof(q_reply);
    constexpr PHSizeType msg_size = q_reply_len-sizeof(PHSizeType);
    
    memset(&q_reply,0,q_reply_len);
    q_reply.Header.Size = FormatData(msg_size);
    if( query_file.size()==0)
    {
      reply_val = 1;
      msg_type = BSEMsgType::QueryReplyErr;
      q_reply.Result.ErrorCode = FormatData(decltype(q_reply.Result.ErrorCode)(reply_val));
    }
    else
    {
      reply_val = 10;
      msg_type = BSEMsgType::QueryReplyOk;
      q_reply.Result.NoOfSubs = FormatData(decltype(q_reply.Result.NoOfSubs)(reply_val));
    }
    q_reply.Header.Type = FormatData(msg_type);
    // send to remote client
    send_data(fd,(char*)&q_reply,q_reply_len);
    printf("Query reply: msg=%d,reply=%d,val=%d\n",req_msg,msg_type,reply_val);
    
    if(query_file.size()>0)
    {
      send_query_file(fd,query_file);
    }
  }

  void BSESimulator::handle_file_notify(int fd)
  {
    char event_buf[256];
    bool if_modified=false;
    while(true)
    {
      int ret = read(fd, event_buf, sizeof(event_buf));
      if(ret <= 0) break;
      else if(ret >= (int)sizeof(inotify_event))
      {
        inotify_event *event = (inotify_event *)&event_buf;
        if(event->mask & IN_CLOSE_WRITE) if_modified = true;
      }
      else break;
    }
    if(if_modified)
    {
      for(auto it = ClientContext_.begin();it != ClientContext_.end();it++)
      {
        auto code = it->first;
        if(it->second->notify_fd == fd)
        {
          printf("Jnl modified, broadcast for Client=%u\n",code);
          send_for_account(it->second);
        }
      }
    }
  }

  void BSESimulator::create_client_context(int fd,const AcInfoPtr &account)
  {
    std::lock_guard<std::mutex> guard(ContextMutex_);
    auto code = account->code;
    auto online = ClientContext_.find(code);
    if(online == ClientContext_.end())
    {
      auto context = std::make_shared<bse_client_context>(account->pb_file);
      if(context->watch_fd>0) // need to monitor pb_file 
      {
        auto handler = std::bind(&BSESimulator::handle_file_notify,this,std::placeholders::_1);
        registry_fd_handler(context->notify_fd,std::move(handler));
      }
      context->clients_fd.emplace(fd);
      context->type = account->type;
      context->q_files = account->q_files;
      context->jnl_file = account->jnl_file;
      context->jnl_rate = account->jnl_rate;
      ClientContext_.emplace(code,std::move(context));
    }
    else
    {
      online->second->clients_fd.emplace(fd);
    }

    auto sock_lookup = SockLookup_.find(fd);
    if(sock_lookup != SockLookup_.end()) sock_lookup->second = code;
  }
  
  void BSESimulator::clear_client_context(int fd,AcCode code)
  {
    std::lock_guard<std::mutex> guard(ContextMutex_);
    auto it =  ClientContext_.find(code);
    if(it != ClientContext_.end())
    {
      auto &clients_fd = it->second->clients_fd;
      auto del_it =  clients_fd.find(fd);
      clients_fd.erase(del_it);
      if(clients_fd.size() == 0)
      {
        if(it->second->notify_fd>0)
          unregistry_fd_handler(it->second->notify_fd);
        
        ClientContext_.erase(code);
      }
    }
    SockLookup_.erase(fd);
  }

  void BSESimulator::on_connect(int fd)
  {
    SockLookup_.emplace(fd,-1);
  }
  
  void BSESimulator::on_disconnect(int fd)
  {
    auto lookup = SockLookup_.find(fd);
    if(lookup == SockLookup_.end()) return;
    
    auto code = lookup->second;
    printf("Disconnect: fd=%d drop from code=%d\n",fd,code);

    clear_client_context(fd,code);
  }

  void BSESimulator::on_error(int fd,int)
  {
    auto lookup = SockLookup_.find(fd);
    if(lookup == SockLookup_.end()) return;
    
    auto code = lookup->second;
    printf("I/O error: fd=%d, code=%d\n",fd,code);

    clear_client_context(fd,code);
  }

  bool BSESimulator::init_from_cfgfile(const std::string &cfg_file)
  {
    std::ifstream ifs(cfg_file);
    if(!ifs.is_open())
    {
      printf("Failed to open config file %s\n",cfg_file.c_str());
      return false;
    }
    
    Json::Reader json_reader;
    Json::Value cfg_root;
    if(!json_reader.parse(ifs,cfg_root))
    {
      printf("Failed to parse config file %s\n",cfg_file.c_str());
      return false;
    }

    auto port = cfg_root["Port"].asUInt();
    auto threads = cfg_root["Threads"].asUInt();
    auto buf_size  = cfg_root["BufSize"].asUInt();

    if(cfg_root["KeepAlive"].isObject())
    {
      auto keep_alive = cfg_root["KeepAlive"];
      if(keep_alive["Enable"].asUInt()==1) KeepAliveSetting_.enable = true;
      else KeepAliveSetting_.enable = false;
      
      KeepAliveSetting_.idle = keep_alive["Idle"].asUInt();
      KeepAliveSetting_.interval = keep_alive["Interval"].asUInt();
      KeepAliveSetting_.count = keep_alive["Count"].asUInt();
    }
    else
    {
      KeepAliveSetting_.enable = false;
    }
    
    auto accounts  = cfg_root["Accounts"];
    auto ac_size = accounts.size();
    for(uint16_t i = 0;i<ac_size;i++)
    {
      auto account = std::make_shared<bse_account>();
      auto ac_code = accounts[i]["Code"].asUInt();
      auto type = ac_type::admin;
      if(accounts[i]["Type"].asUInt() == 1) type = ac_type::broadcast;
      else if(accounts[i]["Type"].asUInt() == 2)
      {
        type = ac_type::query;
        if(accounts[i]["Query"].isArray())
        {
          auto queries = accounts[i]["Query"];
          auto q_size = queries.size();
          for(uint16_t j = 0; j<q_size; j++)
          {
            account->q_files.emplace(queries[j]["Msg"].asUInt(),
                                     queries[j]["QFile"].asString());
          }
        }
      }
      account->type = type;
      account->name = accounts[i]["Name"].asString();
      account->code = ac_code;
      account->pwd = accounts[i]["Pwd"].asString();
      if(accounts[i]["PbFile"].isString()) account->pb_file = accounts[i]["PbFile"].asString();
      if(accounts[i]["JnlFile"].isString()) account->jnl_file = accounts[i]["JnlFile"].asString();
      if(accounts[i]["JnlRate"].isInt()) account->jnl_rate = accounts[i]["JnlRate"].asInt();

      Accounts_.emplace(ac_code,account);
    }    
    ifs.close();

    Set(buf_size,threads);
    return Init(port);
  }
}

static std::string get_parameter(const std::string &cmd)
{
  std::string para="";
  auto split = cmd.find(' ');
  if(std::string::npos != split && cmd.size()>split+1)
  {
    para = cmd.substr(split+1);
  }
  return std::move(para);
}

static bool confirm_command()
{
  std::string input;
  std::cout<<"Confirm command: y/n? ";
  std::getline(std::cin,input);
  if(input == "y") return true;
  else return false;
}

static void print_usage()
{
  std::cout<<"******* Usage *******"<<std::endl;
  std::cout<<"Command list "<<app_version<<std::endl;
  std::cout<<"[p code] to playback jnl file for this account"<<std::endl;
  std::cout<<"[k fd] to kick a client"<<std::endl;
  std::cout<<"[q] to exit!"<<std::endl;
  std::cout<<"[c] to cancel tasks!"<<std::endl;
  std::cout<<"[l] to list clients"<<std::endl;
  std::cout<<"[v] to show version"<<std::endl;
  std::cout<<"******* End *******"<<std::endl;
}

int main(int argc,char **argv)
{
  const std::string cfg_file("bse_server.json");
  VWNet::BSESimulator bse_svr;
  if(!bse_svr.init_from_cfgfile(cfg_file))
  {
    std::cout<<"Init from config file failed!"<<std::endl;
    return 0;
  }

  is_quit_current_task  = false;

  auto task=std::bind(&VWNet::EpollNet::Run,&bse_svr);
  std::thread svr_main(task);
  
  std::unordered_map<char,std::function<bool(const std::string &)>> cmd_proc;
  
  cmd_proc.emplace('q',[&](const std::string&){
      bool is_quit = false;
      if(confirm_command())
      {
        is_quit = true;
        is_quit_current_task = true;
        bse_svr.Stop();
      }
      return is_quit;
    });

  cmd_proc.emplace('c',[&](const std::string &){
      if(confirm_command()) is_quit_current_task = true;
      return false;
    });  

  cmd_proc.emplace('p',[&](const std::string &cmd){
      bse_svr.Command(VWNet::CMD_PLAYBACK,get_parameter(cmd));
      return false;
    });

  cmd_proc.emplace('l',[&](const std::string &){
      bse_svr.Command(VWNet::CMD_LIST);
      return false;
    });

  cmd_proc.emplace('v',[&](const std::string &){
      std::cout<<"Version:"<<app_version<<std::endl;
      return false;
    });  

  cmd_proc.emplace('k',[&](const std::string &cmd){
      //if(confirm_command())
      { bse_svr.Command(VWNet::CMD_KICKOFF,get_parameter(cmd));}
      return false;
    });

  print_usage();
  std::string cmd_line;
  while(true)
  {
    std::cout<<cmd_prompt;
    std::getline(std::cin,cmd_line);
    trim(cmd_line);
    if(cmd_line.size()==1 || (cmd_line.size()>1 && cmd_line[1]==' '))
    {
      auto it = cmd_proc.find(cmd_line[0]);
      if(it != cmd_proc.end())
      {
        if(it->second(cmd_line)) break;
      }
      else print_usage();
    }
    else print_usage();
  }

  svr_main.join();
  
  return 0;
}  

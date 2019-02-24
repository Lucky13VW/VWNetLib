#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <bse.h>
#include <common.h>
#include <json/reader.h>
#include <json/value.h>
#include "thread_pool.hpp"
#include "bse_client.h"

using namespace BSE;

namespace VWNet
{
  /*constexpr decltype(static_cast<LoginRequest*>(nullptr)->ClientCode) client_code = 20033;
  std::string password = "5D7C71634552316197F517B51FA84834402B74EAA3692160ABB3D0C522483563";
  std::string timestamp = "12:11:13";*/

  void BSEClient::on_connect(int fd)
  {
    SockList_.emplace(fd);
    printf("fd=%d connected\n",fd);
    send_login(fd);
    
    jnl_file_ = fopen(account_->jnl_file.c_str(),"a");
    if(jnl_file_ == nullptr )
    {
      printf("file create/open failed:%s\n",strerror(errno));
      return;
    }
  }

  void BSEClient::send_login(int fd) noexcept
  {
    // send login request 
    LoginRequest login_req;
    constexpr auto login_req_len = sizeof(login_req);
    constexpr decltype(login_req.Type) msg_type = BSEMsgType::FeedLogin;
    decltype(static_cast<LoginRequest*>(nullptr)->ClientCode) client_code = account_->code;
    
    memset(&login_req,0,login_req_len);
    login_req.Type = FormatData(msg_type);
    login_req.ClientCode = FormatData(client_code);

    auto pwlen_required = sizeof(login_req.Password);
    auto password = account_->pwd;
    if(password.size()<pwlen_required)
    {
      // space padding at right is required
      auto pwlen_padding = pwlen_required-password.size();
      password.insert(password.size(),pwlen_padding,' ');
    }
    strncpy(login_req.Password,password.c_str(),pwlen_required);
    strncpy(login_req.Timestamp,account_->ts.c_str(),sizeof(login_req.Timestamp));

    printf("fd=%d: send_login(%u,%s,%s)\n",fd,client_code,account_->pwd.c_str(),account_->ts.c_str());
    send_data(fd,reinterpret_cast<char*>(&login_req),login_req_len);
  }

  void BSEClient::send_logoff(int fd) noexcept
  {
    //log off
    LogoffRequest logoff_req;
    constexpr auto logoff_req_len = sizeof(logoff_req);
    constexpr decltype(logoff_req.Type) msg_type = BSEMsgType::FeedLogoff;
    decltype(static_cast<LoginRequest*>(nullptr)->ClientCode) client_code = account_->code;
    
    memset(&logoff_req,0,logoff_req_len);
    logoff_req.Type = FormatData(msg_type);
    logoff_req.ClientCode = FormatData(client_code);

    auto pwlen_required = sizeof(logoff_req.Password);
    auto password = account_->pwd;
    if(password.size()<pwlen_required)
    {
      // space padding at right is required
      auto pwlen_padding = pwlen_required-password.size();
      password.insert(password.size(),pwlen_padding,' ');
    }
    strncpy(logoff_req.Password,password.c_str(),pwlen_required);
    strncpy(logoff_req.Timestamp,account_->ts.c_str(),sizeof(logoff_req.Timestamp));

    printf("fd=%d: send_logoff(%u,%s,%s)\n",fd,client_code,account_->pwd.c_str(),account_->ts.c_str());
    send_data(fd,reinterpret_cast<char*>(&logoff_req),logoff_req_len);
  }
  
  void BSEClient::close_jnl_file() noexcept
  {
    if(jnl_file_ != nullptr)
    {
      fflush(jnl_file_);
      fclose(jnl_file_);
      jnl_file_ = nullptr;
    }
  }

  size_t BSEClient::process_data(int fd,char *data,size_t size)
  {
    size_t size_total = 0;
    size_t size_remain = size;
    while(size_remain > 0)  
    {
      size_t size_process = 0;
      if(size_remain >= sizeof(PacketHeader))
      {
        auto header = reinterpret_cast<PacketHeader*>(data);
    
        size_t size_expect =  FormatData(header->Size)+sizeof(PHSizeType);
        if(size_remain >= size_expect)
        {
          size_process = size_expect;
          switch(FormatData(header->Type))
          {
            case BSEMsgType::FeedLogin:
            {
              auto reply = reinterpret_cast<LoginReply*>(data);
              auto flag = FormatData(reply->SuccessFlag);
              printf("fd=%d: login reply, client=%u,flag=%d\n",fd,FormatData(reply->ClientCode),flag);

              if(flag == 0 && account_->query_msgs.size()>0)
              {
                QueryRequest q_req;
                constexpr auto q_req_len = sizeof(q_req);
                memset(&q_req,0,q_req_len);
                              
                for(auto msg_type: account_->query_msgs)
                {
                  q_req.Type = FormatData(uint32_t(msg_type));
                  send_data(fd,(char*)&q_req,q_req_len);
                  printf("fd=%d: send request, msg_type=%u\n",fd,msg_type);
                }
              }
            }
            break;
            case BSEMsgType::FeedLogoff:
            {
              printf("fd=%d: log off reply\n",fd);
            }
            break;
            case BSEMsgType::QueryReplyErr:
            {
              auto reply = reinterpret_cast<QueryReply*>(data);
              auto error_code = FormatData(reply->Result.ErrorCode);
              printf("fd=%d: query reply, ErrorCode=%d\n",fd,error_code);
            }
            break; 
            case BSEMsgType::QueryReplyOk:
            {
              auto reply = reinterpret_cast<QueryReply*>(data);
              printf("fd=%d: query reply: no of subs:%u\n",fd,FormatData(reply->Result.NoOfSubs));
            } 
            break;
            default:
              save_jnl(fd,data,size_process);
              break;
          }
        }
        //else printf("size_expect=%zd,size_remain=%zd\n",size_expect,size_remain);
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

  size_t BSEClient::save_jnl(int fd,char *data,size_t size) noexcept
  {
    if(jnl_file_ == nullptr) return 0;
    
    ssize_t n_bytes = fwrite(data,sizeof(char),size,jnl_file_);
    if(n_bytes<0) printf("fd=%d: save_jnl error:%s\n",fd,strerror(errno));
    else printf("fd=%d: save_jnl written bytes:%zd\n",fd,n_bytes);

    return n_bytes;
  }

  void BSEClient::handle_quit() noexcept
  {
    for(auto fd:SockList_)
    {
      send_logoff(fd);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Stop();
  }
}

  bool get_config(const std::string &cfg_file,VWNet::client_config &config)
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
    
    config.ip = cfg_root["Ip"].asString();
    config.port = cfg_root["Port"].asUInt();
    config.threads = cfg_root["Threads"].asUInt();
    config.buf_size  = cfg_root["BufSize"].asUInt();

    auto accounts  = cfg_root["Accounts"];
    auto ac_size = accounts.size();
    for(uint16_t i = 0;i<ac_size;i++)
    {
      auto account = std::make_shared<VWNet::client_account>();
      account->name = accounts[i]["Name"].asString();
      account->code = accounts[i]["Code"].asUInt();
      account->pwd = accounts[i]["Pwd"].asString();
      account->ts = accounts[i]["TS"].asString();
      account->jnl_file = accounts[i]["JnlFile"].asString();
      
      if(accounts[i]["Query"].isArray())
      {
        auto queries = accounts[i]["Query"];
        auto q_size = queries.size();
        for(uint16_t j = 0; j<q_size; j++)
        {
          account->query_msgs.push_back(queries[j].asUInt());
        }
      }
      
      config.accounts.emplace_back(account);
    }    
    ifs.close();

    return true;
  }

  int main(int argc,char**argv)
  {
    umask(0);
    VWNet::client_config client_cfg;
    if(!get_config("bse_client.json",client_cfg))
    {
      printf("load bse_client.json error\n");
      return 1;
    }

    std::vector<std::shared_ptr<VWNet::BSEClient>> bse_clients;
    for(auto account : client_cfg.accounts)
    {
      auto bse_client = std::make_shared<VWNet::BSEClient>();
      bse_client->set_account(account);
      bse_client->Set(client_cfg.buf_size,client_cfg.threads);
      bse_client->Init(client_cfg.ip,client_cfg.port);
      bse_clients.emplace_back(bse_client);
    }

    VWNet::ThreadPool thread_pool(bse_clients.size());

    for(auto client: bse_clients)
    {
      auto task = std::bind(&VWNet::EpollNet::Run,client);
      thread_pool.Commit(task);
    }
    thread_pool.Start();

    std::cout<<"input 'q' to quit"<<std::endl;
    std::string cmd_line;
    while(true)
    {
      std::getline(std::cin,cmd_line);
      trim(cmd_line);
      if(cmd_line.size()>0 && cmd_line[0]=='q')
      {
        for(auto client: bse_clients) client->Stop();
        break;
      }
    }
    thread_pool.Stop();

    return 0;
  }

#pragma once

#include <epoll_net.h>
#include <bse.h>
#include <string>
#include <set>

const std::string app_version = "1.3.6";
const std::string cmd_prompt = "$>";

namespace VWNet
{
  using namespace BSE;
  enum class ac_type
  {
    admin,
    broadcast,
    query
  };

  struct bse_account
  {
    ac_type type{ac_type::broadcast};
    int code;
    int jnl_rate;
    std::string name;
    std::string pwd;
    std::string pb_file;
    std::string jnl_file;
    std::unordered_map<int,std::string> q_files;
  };

  struct bse_client_context
  {
    bse_client_context(const std::string &file) noexcept;
    ~bse_client_context();
    
    int notify_fd{0};
    int watch_fd{0};
    int jnl_rate{0};
    ac_type type{ac_type::broadcast};
    std::string pb_file;
    std::string jnl_file;
    std::unordered_map<int,std::string> q_files;
    std::set<int> clients_fd;
  };

  struct keep_alive_para
  {
    bool enable{false}; // if enable keep alive
    int idle{30}; // check after idle
    int interval{3}; // wait time between checking
    int count{2}; // check times
  };
  
  class BSESimulator : public EpollNetTcpServer
  {
    using super = EpollNetTcpServer;
    using AcInfoPtr= std::shared_ptr<bse_account>;
    using CliContextPtr=std::shared_ptr<bse_client_context>;
    using AcCode = int;
    using SockFd = int;
  public:
    BSESimulator(size_t buf_n=BUF_SIZE,uint16_t thread_n=std::thread::hardware_concurrency()) noexcept
      :super(buf_n,thread_n)
    {}
    virtual ~BSESimulator() = default;
    bool init_from_cfgfile(const std::string &cfg_file);

  protected:
    virtual bool start_service() override;
   
  private:
    virtual void add_command() override;
    virtual size_t process_data(int fd,char *data,size_t size) override;
    virtual void on_connect(int fd) override;
    virtual void on_disconnect(int fd) override;
    virtual void on_error(int fd,int err) override;
    
    void handle_login(int fd,LoginRequest *request);
    void handle_logoff(int fd,LogoffRequest *request);
    void handle_request(int fd,QueryRequest *request);
    void handle_file_notify(int fd);
    void command_playback(std::string para);
    void command_list(std::string para);
    void command_kick(std::string para);
    void create_client_context(int fd,const AcInfoPtr &account);
    void clear_client_context(int fd,AcCode code);
    void send_for_account(const CliContextPtr &cli_one_account) noexcept;
    void send_jnl_file(const CliContextPtr &cli_one_account) noexcept;
    void send_query_file(int fd,const std::string &query_file) noexcept;
    std::pair<char*,size_t> map_file(const std::string &file_path) noexcept;
    void unmap_file(char *p_map,size_t m_size) noexcept;
    size_t send_data_all(SockFd fd,char *data,size_t size) noexcept;
    size_t calc_msg_size(PHMsgType type) noexcept;

  private:
    std::unordered_map<AcCode,AcInfoPtr> Accounts_;
    std::unordered_map<SockFd,AcCode> SockLookup_;
    std::unordered_map<AcCode,CliContextPtr> ClientContext_;
    std::mutex ContextMutex_;
    keep_alive_para KeepAliveSetting_;
  };
}

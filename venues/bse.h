#pragma once

namespace BSE{

  enum BSEMsgType
  {
    FeedLogin = 1801,
    FeedLogoff = 1802,
    FeedChangePwd = 2112,
    QueryReplyOk = 2110,
    QueryReplyErr = 2111
 };

  using PHSizeType = uint32_t;
  using PHMsgType = uint32_t;
  
  #pragma pack(push, 1)
  
  struct PacketHeader
  {
    PHSizeType Size;
    uint32_t Type;
  };

  // 1801 login request
  struct LoginRequest
  {
    PHMsgType Type;
    uint16_t ClientCode;
    uint16_t Filler;
    char Password[64];
    char Timestamp[8];
  };

  // 1801 login reply
  struct LoginReply
  {
    PacketHeader Header;
    uint16_t ClientCode;
    int16_t SuccessFlag;
  };

  // 1802 logoff request
  typedef LoginRequest LogoffRequest;
  // 1802 logoff reply
  typedef LoginReply LogoffReply;

  // Change password request 2112
  struct ChangePasswordRequest
  {
  //PacketHeader Header;
    PHMsgType Type;
    char OldPassword[64];
    char NewPassword[64];
  };

  // change password reply 2112
  struct ChangePasswordReply
  {
    PacketHeader Header;
    uint16_t ClientCode;
    int16_t SuccessFlag;
  };

  // to query message
  struct QueryRequest
  {
    PHMsgType Type;
    uint32_t UniqueId;    
  };

  union QueryResult
  {
    uint32_t ErrorCode;
    uint32_t NoOfSubs;
  };
  
  // query reply 2111/2110
  struct QueryReply
  {
    PacketHeader Header;
    QueryResult Result;
  };
  
  #pragma pack(pop) 
}

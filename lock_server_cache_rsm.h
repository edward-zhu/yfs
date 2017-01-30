#ifndef lock_server_cache_rsm_h
#define lock_server_cache_rsm_h

#include <string>
#include <map>
#include <deque>
#include <unordered_set>

#include "rpc/marshall.h"
#include "lock_protocol.h"
#include "rpc/rpc.h"
#include "rsm_state_transfer.h"
#include "rsm.h"
#include "rpc/fifo.h"

class lock_server_cache_rsm : public rsm_state_transfer {
 private:
  struct qitem {
    std::string sender, receiver;
    lock_protocol::lockid_t lid;
    lock_protocol::xid_t xid;
  };

  int nacquire;
  class rsm *rsm;

  fifo<qitem> revoke_queue, retry_queue;
  // std::deque<qitem> _rvq, _rtq;
  std::map<lock_protocol::lockid_t, std::string> _owners;
  std::map<lock_protocol::lockid_t, std::deque<std::string>> _wq;
  std::map<lock_protocol::lockid_t, std::unordered_set<std::string>> _ws;

  std::map<lock_protocol::lockid_t,
    std::map<std::string, lock_protocol::xid_t>> _latest_req;
  std::map<lock_protocol::lockid_t,
    std::map<std::string, int>> _latest_res;

  pthread_mutex_t _m;

 public:
  lock_server_cache_rsm(class rsm *rsm = 0);
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  std::string marshal_state();
  void unmarshal_state(std::string state);
  int acquire(lock_protocol::lockid_t, std::string id,
	      lock_protocol::xid_t, int &);
  int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
	      int &);
  friend marshall & operator<<(marshall &m, const qitem & item);
  friend unmarshall & operator>>(unmarshall &u, qitem &item);
};




#endif


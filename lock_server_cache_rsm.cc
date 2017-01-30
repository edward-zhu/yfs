// the caching lock server implementation

#include "lock_server_cache_rsm.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
revokethread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->retryer();
  return 0;
}

lock_server_cache_rsm::lock_server_cache_rsm(class rsm *_rsm)
  : rsm (_rsm),
    revoke_queue(), retry_queue(),
    _owners(), _wq(), _ws(),
    _latest_req(), _latest_res()
{
  VERIFY(pthread_mutex_init(&_m, NULL) == 0);
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  VERIFY (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  VERIFY (r == 0);
  rsm->set_state_transfer(this);
}

void
lock_server_cache_rsm::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
  while (true) {
    qitem it;
    revoke_queue.deq(&it); // blocking until have one

    if (!rsm->amiprimary()) {
      continue;
    }
    // send revoke
    handle h(it.receiver);
    int rr;
    rlock_protocol::status rret;
    if (h.safebind()) {
      tprintf("[LOCK SRV] send revoke to %s for lock %llu.\n",
          it.receiver.c_str(), it.lid);
      lock_protocol::xid_t xid;
      {
        ScopedLock l(&_m);
        xid = _latest_req[it.lid][it.receiver];
      }
      rret = h.safebind()->call(rlock_protocol::revoke, it.lid, xid, rr);
    }
    if (!h.safebind() || rret != rlock_protocol::OK) {
      tprintf("[LOCK SRV] bind err in revoker loop.\n");
      revoke_queue.enq(it);
    }
  }
}


void
lock_server_cache_rsm::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
  while (true) {
    qitem it;
    retry_queue.deq(&it);
    int rr;
    rlock_protocol::status rret;
    if (!rsm->amiprimary()) {
      continue;
    }
    handle h(it.receiver);
    if (h.safebind()) {
      tprintf("[LOCK SRV] send retry to %s for lock %llu.\n",
          it.receiver.c_str(), it.lid);
      rret = h.safebind()->call(rlock_protocol::retry, it.lid, it.xid, rr);
    }
    if (!h.safebind() || rret != rlock_protocol::OK) {
      tprintf("[LOCK SRV] bind err in retry loop.\n");
      retry_queue.enq(it);
    }
  }
}


int lock_server_cache_rsm::acquire(lock_protocol::lockid_t lid, std::string id,
             lock_protocol::xid_t xid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  std::string holder;
  {
    ScopedLock l(&_m);

    if (_latest_req[lid][id] >= xid) {
      tprintf("[LOCK SRV] old or dup req.\n");
      r = _latest_res[lid][id];
      return ret;
    }

    _latest_req[lid][id] = xid;

    tprintf("[LOCK SRV] %s: acquire lock %llu.\n", id.c_str(), lid);

    // if the lock is free, grant the lock immediately.
    // -- no one is holding the lock, and no one is waiting
    // -- it's the owner
    if ((_owners.count(lid) == 0 && _ws[lid].empty())
        || _owners[lid] == id) {
      r = lock_protocol::OK;
      _latest_res[lid][id] = r;
      _owners[lid] = id;

      tprintf("[LOCK SRV] %s acquired lock %llu granted.\n", id.c_str(), lid);
      // if there is anyone waiting
      if (!_ws[lid].empty()) {
        // send retry to next waiting cli
        retry_queue.enq({id, _wq[lid].front(), lid, xid});
      }
      return ret;
    }

    // if the client is not in the waiting list
    if (_ws[lid].count(id) == 0) {
      _ws[lid].insert(id);
      _wq[lid].push_back(id);
    }

    // can't grant now, please retry
    r = lock_protocol::RETRY;
    _latest_res[lid][id] = r;

    // if this client is the nxt to hold the lock
    if (_wq[lid].front() == id) {
      holder = _owners[lid];
      // send revoke to current owner
      revoke_queue.enq({id, holder, lid, xid});
    }

    return ret;
  }
}

int
lock_server_cache_rsm::release(lock_protocol::lockid_t lid, std::string id,
         lock_protocol::xid_t xid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  ScopedLock l(&_m);
  if (_latest_req[lid][id] == xid && _owners[lid] == id) {
    _owners.erase(lid);
    std::string next = _wq[lid].front();
    // remove from waiting queue
    _ws[lid].erase(next);
    _wq[lid].pop_front();
    // set new owner
    _owners[lid]= next;
    tprintf("[LOCK SRV] %s: release %llu, retry %s.\n",
        id.c_str(), lid, next.c_str());
    // send retry to current owner (waiting for revoke return);
    retry_queue.enq({"", next, lid, xid});
  }
  else {
    tprintf("[LOCK SRV] %s: invalid release lid:%llu xid:%llu.\n",
        id.c_str(), lid, xid);
  }
  r = lock_protocol::OK;
  return ret;
}

std::string
lock_server_cache_rsm::marshal_state()
{
  ScopedLock l(&_m);
  marshall rep;

  rep << _owners;
  rep << _wq;
  rep << _latest_req;
  rep << _latest_res;
  // rep << revoke_queue;
  // rep << retry_queue;

  return rep.str();
}

void
lock_server_cache_rsm::unmarshal_state(std::string state)
{
  ScopedLock l(&_m);
  unmarshall rep(state);
  rep >> _owners;
  rep >> _wq;
  rep >> _latest_req;
  rep >> _latest_res;

  // rep >> revoke_queue;
  // rep >> retry_queue;
}

lock_protocol::status
lock_server_cache_rsm::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

marshall & operator<<(marshall &m, const lock_server_cache_rsm::qitem & item) {
  m << item.sender;
  m << item.receiver;
  m << item.lid;
  m << item.xid;

  return m;
}

unmarshall & operator>>(unmarshall &u, lock_server_cache_rsm::qitem &item) {
  u >> item.sender;
  u >> item.receiver;
  u >> item.lid;
  u >> item.xid;

  return u;
}


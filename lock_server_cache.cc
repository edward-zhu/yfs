// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"
#include "rpc/slock.h"

lock_server_cache::lock_server_cache()
  : nacquire(0), _owners(), _wait_queue(), _wait_set(), _rev_cond()
{
  VERIFY(pthread_mutex_init(&_m, NULL) == 0);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id,
                               int & r)
{
  lock_protocol::status ret = lock_protocol::OK;
  std::string holder;
  {
    ScopedLock l(&_m);
    if (_rev_cond.count(lid) == 0) {
      _rev_cond[lid] = PTHREAD_COND_INITIALIZER;
    }
    // if the lock is free, grant the lock immediately.
    if (_owners.count(lid) == 0 && _wait_queue[lid].empty()) {
      _owners[lid] = id;
      // initialize condition var
      r = lock_protocol::OK;
      tprintf("[LOCK SRV] %s acquired lock %llu granted.\n", id.c_str(), lid);
      return ret;
    }

    // if the client is not in the waiting list
    if (_wait_set[lid].count(id) == 0) {
      _wait_set[lid].insert(id);
      _wait_queue[lid].push(id);
    }

    // if this client is not the next to hold the lock
    if (_wait_queue[lid].front() != id) {
      r = lock_protocol::RETRY;
      tprintf("[LOCK SRV] %s acquired lock %llu retry queue front: %s, sz: %lu.\n",
          id.c_str(), lid, _wait_queue[lid].front().c_str(), _wait_queue[lid].size());
      return ret;
    }

    holder = _owners[lid];
  }

  // otherwise revoke the holder
  handle h(holder);
  int rr;
  rlock_protocol::status rret;
  if (h.safebind()) {
    tprintf("[LOCK SRV] %s send revoke to client %s for lock %llu.\n", id.c_str(), holder.c_str(), lid);
    rret = h.safebind()->call(rlock_protocol::revoke, lid, rr);
  }
  if (!h.safebind() || rret != rlock_protocol::OK) {
    tprintf("[LOCK SRV] bind error cli:%s lock:%llu holder:%s\n", id.c_str(), lid, holder.c_str());
    r = lock_protocol::IOERR;
    return ret;
  }

  // successfully revoked the lock from previous holder.
  std::string to_retry;
  {
    ScopedLock l(&_m);
    // ensure the holder is given up
    tprintf("[LOCK SRV] %s waiting for revoke from %s on lock %llu\n", id.c_str(), holder.c_str(), lid);
    while (_owners.count(lid) != 0) {
      pthread_cond_wait(&_rev_cond[lid], &_m);
    }
    tprintf("[LOCK SRV] %s got lock %llu (revoke returned from %s).\n", id.c_str(), lid, holder.c_str());
    // remove this client from waiting queue/set.
    _wait_set[lid].erase(id);
    _wait_queue[lid].pop();
    // set owner
    _owners[lid] = id;
    tprintf("[LOCK SRV] new wait queue on lock %llu size: %lu.\n", lid, _wait_queue[lid].size());
    // send retry to next waiting client;
    if (!_wait_queue[lid].empty()) {
      to_retry = _wait_queue[lid].front();
    }
    else {
      tprintf("[LOCK SRV] no cli is waiting for lock %llu.\n", lid);
      r = lock_protocol::OK;
      return ret;
    }
    tprintf("[LOCK SRV] %s ready to send retry to %s on lock %llu.\n", id.c_str(), to_retry.c_str(), lid);
  }

  // send retry
  handle rh(to_retry);
  if (rh.safebind()) {
    rret = rh.safebind()->call(rlock_protocol::retry, lid, rr);
  }
  if (!rh.safebind() || rret != rlock_protocol::OK) {
    tprintf("[LOCK SRV] bind err when sending retry cli:%s lock %llu retriee:%s\n",
        id.c_str(), lid, to_retry.c_str());
    r = lock_protocol::IOERR;
    return ret;
  }
  tprintf("[LOCK SRV] cli:%s acquire for %llu returned successfuly\n", id.c_str(), lid);
  r = lock_protocol::OK;
  return ret;
}

int
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id,
         int &r)
{
  ScopedLock l(&_m);
  lock_protocol::status ret = lock_protocol::OK;
  _owners.erase(lid);
  r = lock_protocol::OK;
  pthread_cond_signal(&_rev_cond[lid]);
  tprintf("[LOCK SRV] %s released lock %llu.\n", id.c_str(), lid);
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}


// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache_rsm.h"
#include "rpc/rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

#include "rsm_client.h"

static void *
releasethread(void *x)
{
  lock_client_cache_rsm *cc = (lock_client_cache_rsm *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache_rsm::last_port = 0;

lock_client_cache_rsm::lock_client_cache_rsm(std::string xdst,
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu),
    _stat(), _lc(), _ac(), _ec(), _ws()
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache_rsm::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache_rsm::retry_handler);
  xid = 0;
  // You fill this in Step Two, Lab 7
  // - Create rsmc, and use the object to do RPC
  //   calls instead of the rpcc object of lock_client
  rsmc = new rsm_client(xdst);
  VERIFY(pthread_mutex_init(&_m, NULL) == 0);
  // pthread_t th;
  // int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  // VERIFY (r == 0);
}


void
lock_client_cache_rsm::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.

}

void
lock_client_cache_rsm::_wait(lock_protocol::lockid_t lid, pthread_t tid) {
  _ws[lid].insert(tid);
  lock_stat s = _stat[lid];
  while (s != FREE && s != NONE) {
    pthread_cond_wait(&_lc[lid], &_m);
    s = _stat[lid];
  }
}

lock_protocol::status
lock_client_cache_rsm::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  pthread_t self = pthread_self();

  {
    ScopedLock l(&_m);

    if (_stat.count(lid) == 0) {
      _stat[lid] = NONE;
      _lc[lid] = PTHREAD_COND_INITIALIZER;
      _ac[lid] = PTHREAD_COND_INITIALIZER;
      _ec[lid] = PTHREAD_COND_INITIALIZER;
    }

    if (_stat[lid] != NONE) {
      _wait(lid, self);
      if (_stat[lid] == FREE) {
        _stat[lid] = LOCKED;
        return ret;
      }
    }

    VERIFY(_stat[lid] == NONE);
    _retry_flag[lid] = false;
    _stat[lid] = ACQUIRING;
    _ws[lid].insert(self);
  }

  while (true) {
    lock_protocol::status r, rret;
    lock_protocol::xid_t xid = ++this->xid;
    rret = rsmc->call(lock_protocol::acquire, lid, id, xid, r);
    tprintf("[LOCK CLI] %s acquire(%llu, %llu) returned with %d\n",
        id.c_str(), lid, xid, r);
    {
      ScopedLock l(&_m);
      if (r == lock_protocol::OK) {
        _stat[lid] = LOCKED;
        return ret;
      }
      else if (r == lock_protocol::RETRY) {
        while (!_retry_flag[lid]) {
          pthread_cond_wait(&_ac[lid], &_m);
        }
        _retry_flag[lid] = false;
      }
    }
  }
  return ret;
}

lock_protocol::status
lock_client_cache_rsm::release(lock_protocol::lockid_t lid)
{
  ScopedLock l(&_m);
  pthread_t self = pthread_self();
  _ws[lid].erase(self);
  if (_ws[lid].empty()) {
    pthread_cond_signal(&_ec[lid]);
  }
  else {
    pthread_cond_signal(&_lc[lid]);
  }
  _stat[lid] = FREE;
  return lock_protocol::OK;
}


rlock_protocol::status
lock_client_cache_rsm::revoke_handler(lock_protocol::lockid_t lid,
			          lock_protocol::xid_t xid, int & r)
{
  int ret = rlock_protocol::OK;
  r = rlock_protocol::OK;
  {
    ScopedLock l(&_m);
    while (!_ws[lid].empty()) {
      pthread_cond_wait(&_ec[lid], &_m);
    }
    _stat[lid] = RELEASING;
  }
  int rr;
  if (lu) lu->dorelease(lid);
  rsmc->call(lock_protocol::release, lid, id, xid, rr);
  tprintf("[LOCK CLI] %s release(%llu, %llu) returned with %d\n",
      id.c_str(), lid, xid, rr);
  {
    ScopedLock l(&_m);
    _stat[lid] = NONE;
    pthread_cond_signal(&_lc[lid]);
  }
  return ret;
}

rlock_protocol::status
lock_client_cache_rsm::retry_handler(lock_protocol::lockid_t lid,
			         lock_protocol::xid_t xid, int & r)
{
  int ret = rlock_protocol::OK;
  r = rlock_protocol::OK;
  ScopedLock l(&_m);
  tprintf("[LOCK CLI] %s: retry_req %llu.\n", id.c_str(), lid);
  pthread_cond_signal(&_ac[lid]);
  _retry_flag[lid] = true;
  return ret;
}



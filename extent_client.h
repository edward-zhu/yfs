// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include <map>
#include <pthread.h>

#include "extent_protocol.h"
#include "rpc/rpc.h"

class extent_client {
 private:
  rpcc *cl;

  struct cache_item {
    std::string * buf;
    bool dirty;
    bool removed;
    extent_protocol::attr * attr;

    ~cache_item() {
      delete buf;
      delete attr;
    }
  };

  std::map<extent_protocol::extentid_t, cache_item> _cache;

  pthread_mutex_t _m;

  void _clean_cache(extent_protocol::extentid_t eid);

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid,
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid,
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif


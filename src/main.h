
#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include <pthread.h>
#include <fcgi_stdio.h>
#include <sqlite3.h>
#include <libwebsockets.h>

#include "bstrlib.h"
#include "retval.h"

struct RTPROTO_CLIENT;

struct CCHAT_OP_DATA {
   sqlite3* db;
   pthread_mutex_t db_mutex;
   FCGX_Request req;
   struct RTPROTO_CLIENT* clients;
   size_t clients_sz_max;
   size_t clients_sz;
};

#include "bcgi.h"
#include "webutil.h"
#include "cchat.h"
#include "chatdb.h"
#include "dbglog.h"
#include "rtproto.h"

#endif /* !MAIN_H */



#ifndef MAIN_H
#define MAIN_H

#define _XOPEN_SOURCE 700
#include <time.h>

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
   pthread_mutex_t clients_mutex;
   size_t clients_sz_max;
   size_t clients_sz;
   struct CHATDB_USER* auth_user;
};

#include "bcgi.h"
#include "webutil.h"
#include "assets.h"
#include "cchat.h"
#include "chatdb.h"
#include "dbglog.h"
#include "rtproto.h"

#endif /* !MAIN_H */


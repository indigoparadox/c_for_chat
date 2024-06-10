
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

struct CCHAT_OP_DATA {
   sqlite3* db;
   pthread_mutex_t db_mutex;
   FCGX_Request req;
};

#include "bcgi.h"
#include "webutil.h"
#include "cchat.h"
#include "chatdb.h"
#include "dbglog.h"

#endif /* !MAIN_H */


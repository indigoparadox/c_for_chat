
#ifndef CHATDB_H
#define CHATDB_H

#include <sqlite3.h>

#include "bstrlib.h"
#include "retval.h"

/* TODO: Timestamp. */

typedef int (*chatdb_iter_cb_t)(
   int msg_id, int msg_type, int from, int to, bstring text );

int chatdb_init( bstring path, sqlite3** db_p );

void chatdb_close( sqlite3** db_p );

int chatdb_send_message( sqlite3* db, bstring msg );

#endif /* !CHATDB_H */


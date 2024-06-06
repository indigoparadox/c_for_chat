
#ifndef CHATDB_H
#define CHATDB_H

/* TODO: Timestamp. */

typedef int (*chatdb_iter_cb_t)(
   bstring page_text,
   int msg_id, int msg_type, int from, int to, bstring text, time_t msg_time );

int chatdb_init( bstring path, sqlite3** db_p );

void chatdb_close( sqlite3** db_p );

int chatdb_send_message( sqlite3* db, bstring msg, bstring* err_msg_p );

int chatdb_iter_messages(
   bstring page_text, sqlite3* db,
   int msg_type, int dest_id, chatdb_iter_cb_t cb );

#endif /* !CHATDB_H */


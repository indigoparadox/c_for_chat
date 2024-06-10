
#ifndef CHATDB_H
#define CHATDB_H

/* TODO: Timestamp. */

#define CHATDB_OPTION_FMT_INT 0
#define CHATDB_OPTION_FMT_STR 1

union CHATDB_OPTION_VAL {
   int integer;
   bstring str;
};

typedef int (*chatdb_iter_msg_cb_t)(
   struct WEBUTIL_PAGE* page,
   int msg_id, int msg_type, bstring from, int to, bstring text, time_t msg_time );

typedef int (*chatdb_iter_user_cb_t)(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   bstring password_test, int* user_id_out_p, int* session_timeout_p,
   int user_id, bstring user_name, bstring email,
   bstring hash, size_t hash_sz, bstring salt, size_t iters, time_t msg_time,
   int session_timeout );

typedef int (*chatdb_iter_session_cb_t)(
   struct WEBUTIL_PAGE* page, int* user_id_out_p, int session_id, int user_id,
   bstring hash, size_t hash_sz, bstring remote_host, time_t start_time );

int chatdb_init( bstring path, struct CCHAT_OP_DATA* op );

void chatdb_close( struct CCHAT_OP_DATA* op );

int chatdb_add_user(
   struct CCHAT_OP_DATA* op, int user_id, bstring user, bstring password, bstring email,
   bstring session_timeout, bstring* err_msg_p );

int chatdb_send_message(
   struct CCHAT_OP_DATA* op, int user_id, bstring msg, bstring* err_msg_p );

int chatdb_iter_messages(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   int msg_type, int dest_id, chatdb_iter_msg_cb_t cb, bstring* err_msg_p);

int chatdb_iter_users(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   bstring user_name, int user_id, bstring password_test, int* user_id_out_p,
   int* session_timeout_p, chatdb_iter_user_cb_t cb, bstring* err_msg_p );

int chatdb_add_session(
   struct CCHAT_OP_DATA* op, int user_id, bstring remote_host, bstring* hash_p,
   bstring* err_msg_p );

int chatdb_iter_sessions(
   struct WEBUTIL_PAGE* page, int* user_id_out_p, struct CCHAT_OP_DATA* op,
   bstring hash, bstring remote_host,
   chatdb_iter_session_cb_t cb, bstring* err_msg_p );

int chatdb_remove_session(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op, bstring hash, bstring* err_msg_p );

int chatdb_get_option(
   const char* key, union CHATDB_OPTION_VAL* val,
   struct CCHAT_OP_DATA* op, bstring* err_msg_p );

int chatdb_set_option(
   const char* key, union CHATDB_OPTION_VAL* val, int format,
   struct CCHAT_OP_DATA* op, bstring* err_msg_p );

#endif /* !CHATDB_H */


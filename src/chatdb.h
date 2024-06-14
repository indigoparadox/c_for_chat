
#ifndef CHATDB_H
#define CHATDB_H

/* TODO: Timestamp. */

#define CHATDB_USER_FLAG_WS      0x01

#define CHATDB_OPTION_FMT_INT 0
#define CHATDB_OPTION_FMT_STR 1

/* Second field is prepared statement index. */
/* If the second field < 0, this field won't be added to inserts/updates. */
#define CHATDB_USER_TABLE( f ) \
   f( user,  0, -1, user_id,         int,     "integer primary key" ) \
   f( user,  1,  1, user_name,       bstring, "text not null unique" ) \
   f( user,  2,  2, email,           bstring, "text" ) \
   f( user,  3,  3, hash,            bstring, "text not null" ) \
   f( user,  4,  4, hash_sz,         int,     "integer not null" ) \
   f( user,  5,  5, salt,            bstring, "text not null" ) \
   f( user,  6,  6, iters,           int,     "integer not null" ) \
   f( user,  7, -1, join_time, time_t,  "datetime default current_timestamp" ) \
   f( user,  8,  7, session_timeout, int,     "integer default 3600" ) \
   f( user,  9,  8, flags,           int,     "integer default 0" ) \
   f( user, 10,  9, time_fmt,  bstring, "text default '%Y-%m-%d %H:%M %Zs'" ) \
   f( user, 11, 10, timezone,  int,           "integer default 0" )

#define CHATDB_MESSAGE_TABLE( f ) \
   f( message, 0, -1, msg_id,             int,     "integer primary key" ) \
   f( message, 1,  1, msg_type,           int,     "integer not null" ) \
   f( message, 2,  2, user_from_id,       int,     "integer not null" ) \
   f( message, 3,  3, room_or_user_to_id, int,     "integer not null" ) \
   f( message, 4,  4, msg_text,           bstring, "text" ) \
   f( message, 5, -1, msg_time,  time_t, "datetime default current_timestamp" )

#define chatdb_free_user( u ) \
   bcgi_cleanup_bstr( (u)->user_name, likely ) \
   bcgi_cleanup_bstr( (u)->email, likely ) \
   bcgi_cleanup_bstr( (u)->hash, likely ) \
   bcgi_cleanup_bstr( (u)->salt, likely ) \
   bcgi_cleanup_bstr( (u)->time_fmt, likely )

#define chatdb_free_message( m ) \
   bcgi_cleanup_bstr( (m)->msg_text, likely )

#define CHATDB_TABLE_STRUCT( v, idx, u, field, c_type, db_type ) c_type field;

#define CHATDB_TABLE_ASSIGN( v, idx, u, field, c_type, db_type ) \
   chatdb_assign_ ## c_type ( &(arg_struct->v->field), argv[idx] );

struct CHATDB_USER {
   CHATDB_USER_TABLE( CHATDB_TABLE_STRUCT )
};

struct CHATDB_MESSAGE {
   CHATDB_MESSAGE_TABLE( CHATDB_TABLE_STRUCT )
};

union CHATDB_OPTION_VAL {
   int integer;
   bstring str;
};

typedef int (*chatdb_iter_msg_cb_t)(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   struct CHATDB_MESSAGE* message, bstring user_name );

typedef int (*chatdb_iter_user_cb_t)(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op, bstring password,
   struct CHATDB_USER* user );

typedef int (*chatdb_iter_session_cb_t)(
   struct WEBUTIL_PAGE* page, int* user_id_out_p, int session_id, int user_id,
   bstring hash, size_t hash_sz, bstring remote_host, time_t start_time );

int chatdb_init( bstring path, struct CCHAT_OP_DATA* op );

void chatdb_close( struct CCHAT_OP_DATA* op );

int chatdb_add_user(
   struct CCHAT_OP_DATA* op, struct CHATDB_USER* user, bstring password,
   bstring* err_msg_p );

int chatdb_send_message(
   struct CCHAT_OP_DATA* op, int user_id, bstring msg, bstring* err_msg_p );

int chatdb_iter_messages(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   struct CHATDB_MESSAGE* message, chatdb_iter_msg_cb_t cb, bstring* err_msg_p
);

int chatdb_iter_users(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op, bstring password_test,
   struct CHATDB_USER* user, chatdb_iter_user_cb_t cb, bstring* err_msg_p );

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


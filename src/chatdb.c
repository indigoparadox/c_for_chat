
#include "main.h"

#include <stdlib.h> /* for atoi() */

#define CHATDB_HASH_SZ 32
#define CHATDB_SESSION_HASH_SZ 32
#define CHATDB_SALT_SZ 32
#define CHATDB_PASSWORD_ITER 2000

#define chatdb_argv( field_name, idx ) \
   field_name = bfromcstr( argv[idx] ); \
   if( NULL == field_name ) { \
      dbglog_error( "error allocating " #field_name "!\n" ); \
      retval = RETVAL_ALLOC; \
      goto cleanup; \
   }

#define CHATDB_USER_TABLE_DB_FIELDS( idx, field, c_type, db_type ) \
   static const struct tagbstring \
      _chatdb_field_users_ ## field = bsStatic( #field " " db_type );

CHATDB_USER_TABLE( CHATDB_USER_TABLE_DB_FIELDS );

#define CHATDB_USER_TABLE_DB_FIELDS_LIST( idx, field, c_type, db_type ) \
   &_chatdb_field_users_ ## field,

static const struct bstrList _gc_chatdb_fields_users = {
   9, 9,
   {
      CHATDB_USER_TABLE( CHATDB_USER_TABLE_DB_FIELDS_LIST )
   }
};

struct CHATDB_ARG {
   chatdb_iter_msg_cb_t cb_msg;
   chatdb_iter_user_cb_t cb_user;
   chatdb_iter_session_cb_t cb_session;
   struct WEBUTIL_PAGE* page;
   bstring password_test;
   struct CCHAT_OP_DATA* op;
   int* user_id_out_p;
   struct CHATDB_USER* user;
};

int chatdb_init( bstring path, struct CCHAT_OP_DATA* op ) {
   int retval = 0;
   char* err_msg = NULL;
   union CHATDB_OPTION_VAL schema_ver;
   bstring query = NULL;

   retval = sqlite3_open( bdata( path ), &(op->db) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not open database!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   assert( NULL != op->db );

   /* Create message table if it doesn't exist. */
   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db,
      "create table if not exists messages( "
         "msg_id integer primary key,"
         "msg_type integer not null,"
         "user_from_id integer not null,"
         "room_or_user_to_id integer not null,"
         "msg_text text,"
         "msg_time datetime default current_timestamp );", NULL, 0, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database message table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

#if 0
   #define CHATDB_USER_TABLE_DB_FIELDS( idx, field, c_type, db_type ) \
      #field " " db_type ", "

   dbglog_debug( 1, "create table if not exists users( "
         CHATDB_USER_TABLE( CHATDB_USER_TABLE_DB_FIELDS )
         ");\n" );
#endif

   query = bjoinStatic( &_gc_chatdb_fields_users, "," );
   bcgi_check_null( query );

   dbglog_debug( 1, "%s\n", bdata( query ) );
   
   exit( 1 );

   /* Create user table if it doesn't exist. */
   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, bdata( query ), NULL, 0, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database user table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create user table if it doesn't exist. */
   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db,
      "create table if not exists sessions( "
         "session_id integer primary key, "
         "user_id integer not null, "
         "hash text not null, "
         "hash_sz integer not null, "
         "remote_host text not null, "
         "start_time datetime default current_timestamp );",
      NULL, 0, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database session table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create options table if it doesn't exist. */
   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db,
      "create table if not exists options( "
         "option_id integer primary key, "
         "key text not null unique, "
         "value text not null, "
         "format integer not null );",
      NULL, 0, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database options table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Check to see if we're running an old schema. */
   schema_ver.integer = 0;
   retval = chatdb_get_option( "schema_version", &schema_ver, op, NULL );
   if( retval ) {
      dbglog_error( "error getting schema version!\n" );
      goto cleanup;
   }
   dbglog_debug( 9, "schema version: %d\n", schema_ver.integer );
   switch( schema_ver.integer ) {
   case 0:
      /* Bring up to version one. */
      pthread_mutex_lock( &(op->db_mutex) );
      retval = sqlite3_exec( op->db,
         "alter table users add column session_timeout integer default 3600",
         NULL, 0, &err_msg );
      pthread_mutex_unlock( &(op->db_mutex) );
      if( SQLITE_OK != retval ) {
         dbglog_error( "could not update users table: %s\n", err_msg );
         retval = RETVAL_DB;
         sqlite3_free( err_msg );
         goto cleanup;
      }
      schema_ver.integer = 1;
      retval = chatdb_set_option( 
         "schema_version", &schema_ver, CHATDB_OPTION_FMT_INT, op, NULL );
      if( retval ) {
         dbglog_error( "error setting schema version!\n" );
         goto cleanup;
      }
      dbglog_debug( 9, "updated schema version: %d\n", schema_ver.integer );
      break;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   bcgi_cleanup_bstr( query, likely );

   return retval;
}

void chatdb_close( struct CCHAT_OP_DATA* op ) {

   sqlite3_close( op->db );
   op->db = NULL;

}

int chatdb_add_user(
   struct CCHAT_OP_DATA* op, int user_id, bstring user, bstring password, bstring email,
   bstring session_timeout, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   unsigned char* salt_str = NULL;
   bstring hash = NULL;
   bstring salt = NULL;

   if( 0 == blength( user ) ) {
      *err_msg_p = bfromcstr( "Username cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   if( 0 > user_id && 0 == blength( password ) ) {
      *err_msg_p = bfromcstr( "New password cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   if( 0 > user_id || 0 < blength( password ) ) {
   
      /* Generate a new salt. */
      retval = bcgi_generate_salt( &salt, CHATDB_SALT_SZ );

      /* Hash the provided password. */
      retval = bcgi_hash_password(
         password, CHATDB_PASSWORD_ITER, CHATDB_HASH_SZ, salt, &hash );
      if( retval ) {
         goto cleanup;
      }

   }

   if( 0 > user_id ) {
      /* Generate an "add user" query. */
      query = sqlite3_mprintf(
         "insert into users "
         "(user_name, email, hash, hash_sz, salt, iters, session_timeout) "
         "values('%q', '%q', '%q', '%d', '%q', '%d', '%q')",
         bdata( user ), bdata( email ), bdata( hash ), CHATDB_HASH_SZ,
         bdata( salt ), CHATDB_PASSWORD_ITER, bdata( session_timeout ) );

      dbglog_debug( 1, "attempting to add user %s...\n", bdata( user ) );
   } else {
      /* Generate an "edit user" query. */
      /* TODO: Add password if provided. */
      query = sqlite3_mprintf(
         "update users set "
            "user_name = '%q', "
            "email = '%q', "
            "session_timeout = '%q' "
            "where user_id = '%d'",
         bdata( user ), bdata( email ), bdata( session_timeout ), user_id );

      dbglog_debug( 1, "updating user %d...\n", user_id );
   }

   /* Check query. */
   if( NULL == query ) {
      dbglog_error( "could not allocate database user insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, query, NULL, NULL, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      goto cleanup;
   }

cleanup:

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != salt ) {
      bdestroy( salt );
   }

   if( NULL != salt_str ) {
      free( salt_str );
   }

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

int chatdb_send_message(
   struct CCHAT_OP_DATA* op, int user_id, bstring msg, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   /* TODO: Actual to/from. */
   query = sqlite3_mprintf(
      "insert into messages "
      "(msg_type, user_from_id, room_or_user_to_id, msg_text) "
      "values(0, '%d', 0, '%q')",
      user_id, bdata( msg ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database chat insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, query, NULL, NULL, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      /* retval = RETVAL_DB; */
      retval = 0;
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      goto cleanup;
   }

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

static
int chatdb_dbcb_messages( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;
   bstring user_name = NULL;
   bstring msg_text = NULL;

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   chatdb_argv( user_name, 2 );
   chatdb_argv( msg_text, 4 );

   retval = arg_struct->cb_msg(
      arg_struct->page,
      atoi( argv[0] ), /* msg_id */
      atoi( argv[1] ), /* msg_type */
      user_name, /* user_from_id */
      atoi( argv[3] ), /* room_or_user_to_id */
      msg_text,
      atoi( argv[5] ) ); /* msg_time */

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   if( NULL != user_name ) {
      bdestroy( user_name );
   }

   return retval;
}

int chatdb_iter_messages(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   int msg_type, int dest_id, chatdb_iter_msg_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;

   arg_struct.cb_msg = cb;
   arg_struct.page = page;

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db,
      "select m.msg_id, m.msg_type, u.user_name, m.room_or_user_to_id, "
         "m.msg_text, strftime('%s', m.msg_time) from messages m "
         "inner join users u on u.user_id = m.user_from_id "
         "order by m.msg_time desc limit 100",
      chatdb_dbcb_messages, &arg_struct, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      /* TODO: Return err_msg. */
      dbglog_error( "could not execute database message query: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      if( NULL != err_msg ) {
         sqlite3_free( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   return retval;
}

static
int chatdb_assign_time_t( time_t* out_p, const char* in ) {
   int retval = 0;
   struct tm ts;
   char buffer[101];

   memset( buffer, '\0', 101 );

   strptime( in, "%Y-%m-%d%t%H:%M:%S", &ts );
   strftime( buffer, 100, "%s", &ts );
   /* TODO: Don't use atoi() for this! */
   *out_p = atol( buffer );

   return retval;
}

static
int chatdb_assign_int( int* out_p, const char* in ) {
   int retval = 0;

   *out_p = atoi( in );

   return retval;
}

static
int chatdb_assign_bstring( bstring* out_p, const char* in ) {
   int retval = 0;

   if( NULL != *out_p ) {
      retval = bassigncstr( *out_p, in );
      bcgi_check_bstr_err( *out_p );
   } else {
      *out_p = bfromcstr( in );
      bcgi_check_null( *out_p );
   }

cleanup:

   return retval;
}

static
int chatdb_dbcb_users( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;

   if( 8 > argc ) {
      dbglog_error( "incorrect number of user fields!\n" );
      retval = 1;
      goto cleanup;
   }

   #define CHATDB_USER_TABLE_ASSIGN( idx, field, c_type, db_type ) \
      chatdb_assign_ ## c_type ( &(arg_struct->user->field), argv[idx] );

   CHATDB_USER_TABLE( CHATDB_USER_TABLE_ASSIGN );

#if 0
   user_name = bfromcstr( argv[1] );
   if( NULL == user_name ) { 
      dbglog_error( "error allocating user_name!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   email = bfromcstr( argv[2] );
   if( NULL == email && NULL != argv[2] ) { 
      dbglog_error( "error allocating email!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   hash = bfromcstr( argv[3] );
   if( NULL == hash ) { 
      dbglog_error( "error allocating hash!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   assert( NULL == arg_struct->user->hash );
   arg_struct->user->salt = bfromcstr( argv[5] );
   bcgi_check_null( arg_struct->user->salt );

   assert( NULL == arg_struct->user->salt );
   arg_struct->user->salt = bfromcstr( argv[5] );
   bcgi_check_null( arg_struct->user->salt );
#endif

   retval = arg_struct->cb_user(
      arg_struct->page,
      arg_struct->op,
      arg_struct->password_test,
      arg_struct->user );

cleanup:

#if 0
   if( NULL != user_name ) {
      bdestroy( user_name );
   }

   if( NULL != email ) {
      bdestroy( email );
   }

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != salt ) {
      bdestroy( salt );
   }
#endif

   return retval;
}

int chatdb_iter_users(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op, bstring password_test,
   struct CHATDB_USER* user, chatdb_iter_user_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;
   char* query = NULL;
   char* dyn_query = NULL;

   arg_struct.cb_user = cb;
   arg_struct.page = page;
   arg_struct.user = user;
   arg_struct.password_test = password_test;
   arg_struct.op = op;

   #define CHATDB_USER_TABLE_SELECT( idx, field, c_type, db_type ) \
      #field,

   if( NULL != user->user_name ) {
      assert( 0 > user->user_id );
      dyn_query = sqlite3_mprintf(
         "select * from users where user_name = '%q'",
         bdata( user->user_name ) );
      query = dyn_query;
   } else if( 0 < user->user_id ) {
      dyn_query = sqlite3_mprintf(
         "select * from users where user_id = %d",
         user->user_id );
      query = dyn_query;
   } else {
      query = "select * from users";
   }

   if( NULL == query ) {
      dbglog_error( "could not allocate database user select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval =
      sqlite3_exec( op->db, query, chatdb_dbcb_users, &arg_struct, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      /* Return err_msg. */
      dbglog_error(
         "could not execute database user query: %s\n", err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   if( NULL != dyn_query ) {
      sqlite3_free( dyn_query );
   }

   return retval;
}

int chatdb_add_session(
   struct CCHAT_OP_DATA* op, int user_id, bstring remote_host, bstring* hash_p,
   bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   retval = bcgi_generate_salt( hash_p, CHATDB_SALT_SZ );
   if( retval ) {
      goto cleanup;
   }

   /* Trim off trailing '='. */
   while( BSTR_ERR != bstrrchr( *hash_p, '=' ) ) {
      retval = btrunc( *hash_p, blength( *hash_p ) - 1 );
      if( BSTR_ERR == retval ) {
         dbglog_error( "error truncating hash!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }
   retval = 0;

   /* Store the user record. */
   query = sqlite3_mprintf(
      "insert into sessions "
      "(user_id, hash, hash_sz, remote_host) "
      "values(%d, '%q', %d, '%q')",
      user_id, bdata( *hash_p ), CHATDB_SESSION_HASH_SZ, bdata( remote_host ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, query, NULL, NULL, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      *err_msg_p = bfromcstr( err_msg );
      goto cleanup;
   }

cleanup:

   return retval;
}

static
int chatdb_dbcb_sessions( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;
   bstring hash = NULL;
   bstring remote_host = NULL;

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   chatdb_argv( hash, 2 );
   chatdb_argv( remote_host, 4 );

   retval = arg_struct->cb_session(
      arg_struct->page,
      arg_struct->user_id_out_p,
      atoi( argv[0] ), /* session_id */
      atoi( argv[1] ), /* user_id */
      hash,
      atoi( argv[3] ), /* hash_sz */
      remote_host,
      atoi( argv[5] ) ); /* start_time */

cleanup:

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != remote_host ) {
      bdestroy( remote_host );
   }

   return retval;
}

int chatdb_iter_sessions(
   struct WEBUTIL_PAGE* page, int* user_id_out_p, struct CCHAT_OP_DATA* op,
   bstring hash, bstring remote_host,
   chatdb_iter_session_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;
   char* query = NULL;

   arg_struct.cb_session = cb;
   arg_struct.page = page;
   arg_struct.user_id_out_p = user_id_out_p;

   assert( NULL != op->db );

   query = sqlite3_mprintf(
      "select session_id, user_id, hash, hash_sz, remote_host, "
         "strftime('%%s', start_time) from sessions "
         "where hash = '%q' and remote_host = '%q'",
      bdata( hash ), bdata( remote_host ) );
   /* XXX */
   dbglog_debug( 1, "query: %s\n", query );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec(
      op->db, query, chatdb_dbcb_sessions, &arg_struct, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not execute database session query: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}


int chatdb_remove_session(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op, bstring hash, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   char* query = NULL;

   query = sqlite3_mprintf(
      "delete from sessions where hash = '%q'", bdata( hash ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session delete!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, query, NULL, NULL, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not execute database session delete: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;

}

static
int chatdb_dbcb_options( void* arg, int argc, char** argv, char **col ) {
   int retval = 0;
   union CHATDB_OPTION_VAL* val_p = (union CHATDB_OPTION_VAL*)arg;
   int format = 0;

   format = atoi( argv[2] );

   switch( format ) {
   case CHATDB_OPTION_FMT_INT:
      val_p->integer = atoi( argv[1] );
      break;

   case CHATDB_OPTION_FMT_STR:
      val_p->str = bfromcstr( argv[1] );
      break;
   }

   return retval;
}

int chatdb_get_option(
   const char* key, union CHATDB_OPTION_VAL* val,
   struct CCHAT_OP_DATA* op, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   char* query = NULL;

   query = sqlite3_mprintf(
      "select key, value, format from options where key = '%q'", key );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec(
      op->db, query, chatdb_dbcb_options, val, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not execute database session query: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

int chatdb_set_option(
   const char* key, union CHATDB_OPTION_VAL* val, int format,
   struct CCHAT_OP_DATA* op, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   bstring value_set = NULL;

   switch( format ) {
   case CHATDB_OPTION_FMT_INT:
      value_set = bformat( "%d", val->integer );
      if( NULL == value_set ) {
         /* This might be invalid input, so don't make it the harder "ALLOC". */
         dbglog_error( "unable to parse integer value!\n" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }
      break;

   case CHATDB_OPTION_FMT_STR:
      value_set = bstrcpy( val->str );
      bcgi_check_null( value_set );
      break;

   default:
      dbglog_error( "invalid option format specified!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* Generate an insert query. */
   query = sqlite3_mprintf(
      "insert into options "
      "(key, value, format) values('%q', '%q', '%d') "
      "on conflict(key) do update set value=excluded.value",
      key, bdata( value_set ), format );
   bcgi_check_null( query );

   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, query, NULL, NULL, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      goto cleanup;
   }

cleanup:

   bcgi_cleanup_bstr( value_set, likely );

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}


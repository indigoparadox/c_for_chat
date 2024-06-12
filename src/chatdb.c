
#include "main.h"

#include <stdlib.h> /* for atoi() */

#define CHATDB_HASH_SZ 32
#define CHATDB_SESSION_HASH_SZ 32
#define CHATDB_SALT_SZ 32
#define CHATDB_PASSWORD_ITER 2000

#define CHATDB_INSERT_FLAG_SKIP_UNIQUE 0x01

#define chatdb_argv( field_name, idx ) \
   field_name = bfromcstr( argv[idx] ); \
   if( NULL == field_name ) { \
      dbglog_error( "error allocating " #field_name "!\n" ); \
      retval = RETVAL_ALLOC; \
      goto cleanup; \
   }

#define CHATDB_USER_TABLE_DB_FIELDS( idx, u, field, c_type, db_type ) \
   #field "*" #u "$" db_type "|"

#define CHATDB_USER_TABLE_PREPARE( idx, u, field, c_type, db_type ) \
   dbglog_debug( 1, "binding user->" #field " as param %d...\n", i ); \
   assert( 0 != user->field || NULL == strstr( db_type, "not null" ) ); \
   retval = _chatdb_bind_ ## c_type ( stmt, &i, user->field ); \
   if( retval ) { \
      dbglog_error( "error binding field: " #field "!\n" ); \
      assert( NULL == *err_msg_p ); \
      *err_msg_p = bfromcstr( "Error binding field: " #field "\n" ); \
      pthread_mutex_unlock( &(op->db_mutex) ); \
      goto cleanup; \
   } \

#define _chatdb_alter_table( version, db, mutex, alter_stmt ) \
      pthread_mutex_lock( mutex ); \
      retval = sqlite3_exec( db, alter_stmt, NULL, 0, &err_msg ); \
      pthread_mutex_unlock( mutex ); \
      if( SQLITE_OK != retval ) { \
         dbglog_error( "could not update table: %s\n", err_msg ); \
         retval = RETVAL_DB; \
         sqlite3_free( err_msg ); \
         goto cleanup; \
      } \
      schema_ver.integer = version; \
      retval = chatdb_set_option(  \
         "schema_version", &schema_ver, CHATDB_OPTION_FMT_INT, op, NULL ); \
      if( retval ) { \
         dbglog_error( "error setting schema version!\n" ); \
         goto cleanup; \
      } \
      dbglog_debug( 9, "updated schema version: %d\n", schema_ver.integer );

const static struct tagbstring _gc_chatdb_fields_users = 
   bsStatic( CHATDB_USER_TABLE( CHATDB_USER_TABLE_DB_FIELDS ) );

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

static int _chatdb_build_split_fields(
   struct bstrList** lst_p, const_bstring fields
) {
   int retval = 0;

   assert( NULL == *lst_p );

   /* Split field list and join it with commas. */
   *lst_p = bsplit( &_gc_chatdb_fields_users, '|' );
   bcgi_check_null( *lst_p );

   /* Delete empty last entry. */
   assert( (*lst_p)->qty > 1 );
   bdestroy( (*lst_p)->entry[(*lst_p)->qty - 1] );
   (*lst_p)->qty--;

cleanup:
 
   return retval;
}

static int _chatdb_build_create_table(
   bstring* query_p, const_bstring fields, const char* table_name
) {
   int retval = 0;
   bstring list_str = NULL;
   size_t i = 0;
   struct bstrList* field_list = NULL;
   struct bstrList* key_list = NULL;
   bstring props = NULL;
   int sep_1_pos = 0;
   int sep_2_pos = 0;

   assert( NULL == *query_p );

   retval = _chatdb_build_split_fields( &field_list, fields );
   if( retval ) {
      goto cleanup;
   }

   /* Create a list to hold truncated keys to used fields. */
   key_list = bstrListCreate();
   bcgi_check_null( key_list );
   retval = bstrListAlloc( key_list, field_list->qty );
   bcgi_check_bstr_err( key_list );

   props = bfromcstr( "" );
   bcgi_check_null( props );
   
   /* Replace star separators with spaces. */
   for( i = 0 ; field_list->qty > i ; i++ ) {
      assert( NULL != field_list->entry[i]->data );

      sep_1_pos = bstrchr( field_list->entry[i], '*' );
      sep_2_pos = bstrchr( field_list->entry[i], '$' ) + 1;

      assert( BSTR_ERR != sep_1_pos );
      assert( BSTR_ERR != sep_2_pos );

      /* Get the field name. */
      key_list->entry[i] = bmidstr( field_list->entry[i], 0, sep_1_pos );
      bcgi_check_null( key_list->entry[i] );
      retval = bconchar( key_list->entry[i], ' ' );
      bcgi_check_bstr_err( key_list->entry[i] );

      /* Get the field properties. */
      retval = bassignmidstr(
         props, field_list->entry[i],
         sep_2_pos, blength( field_list->entry[i] ) - sep_2_pos );
      bcgi_check_bstr_err( props );

      retval = bconcat( key_list->entry[i], props );
      bcgi_check_bstr_err( key_list->entry[i] );

      key_list->qty++;
   }

   /* Join split field names into list. */
   list_str = bjoinStatic( key_list, ", " );
   bcgi_check_null( list_str );

   *query_p = bformat( "create table if not exists %s( %s );",
      table_name, bdata( list_str ) );
   bcgi_check_null( *query_p );

cleanup:

   bcgi_cleanup_bstr( list_str, likely );
   bcgi_cleanup_bstr( props, likely );

   if( NULL != field_list ) {
      bstrListDestroy( field_list );
   }

   if( NULL != key_list ) {
      bstrListDestroy( key_list );
   }

   return retval;
}

static int _chatdb_build_update(
   bstring* query_p, const_bstring fields, const char* table_name
) {
   int retval = 0;
   const struct tagbstring cs_primary = bsStatic( "primary key" );
   const struct tagbstring cs_no_insert = bsStatic( "*-1$" );
   struct bstrList* field_list = NULL;
   struct bstrList* update_list = NULL;
   bstring update_str = NULL;
   int sep_pos = 0;
   size_t i = 0;
   bstring primary_key = NULL;

   retval = _chatdb_build_split_fields( &field_list, fields );
   if( retval ) {
      goto cleanup;
   }

   /* Create a list to hold contingency update statements. */
   update_list = bstrListCreate();
   bcgi_check_null( update_list );
   retval = bstrListAlloc( update_list, field_list->qty );
   bcgi_check_bstr_err( update_list );

   for( i = 0 ; field_list->qty > i ; i++ ) {
      assert( NULL != field_list->entry[i]->data );

      sep_pos = bstrchr( field_list->entry[i], '*' );
      assert( BSTR_ERR != sep_pos );

      if(
         BSTR_ERR != binstrcaseless(
            field_list->entry[i], sep_pos, &cs_primary )
      ) {
         dbglog_debug( 1, "skipping primary key: %s\n",
            bdata( field_list->entry[i] ) );
         primary_key = bmidstr( field_list->entry[i], 0, sep_pos );
         bcgi_check_null( primary_key );
         continue;

      } else if(
         BSTR_ERR != binstr( field_list->entry[i], sep_pos, &cs_no_insert )
      ) {
         dbglog_debug( 1, "skipping no-insert field: %s\n",
            bdata( field_list->entry[i] ) );
         continue;
      }

      /* Build contingency update statement. */
      update_list->entry[update_list->qty] = bmidstr(
         field_list->entry[i], 0, sep_pos );
      bcgi_check_null( update_list->entry[update_list->qty] );
      retval = bcatcstr( update_list->entry[update_list->qty], " = ?" );
      bcgi_check_bstr_err( update_list->entry[update_list->qty] );

      update_list->qty++;
   }

   update_str = bjoinStatic( update_list, ", " );

   /* Build the query string. */
   if( NULL == *query_p ) {
      *query_p = bfromcstr( "" );
      bcgi_check_null( *query_p );
   }
   retval = bassignformat(
      *query_p, "update %s set %s where %s = ?",
      table_name, bdata( update_str ), bdata( primary_key ) );
   bcgi_check_bstr_err( *query_p );

cleanup:

   bcgi_cleanup_bstr( update_str, likely );
   bcgi_cleanup_bstr( primary_key, likely );

   if( NULL != update_list ) {
      bstrListDestroy( update_list );
   }

   if( NULL != field_list ) {
      bstrListDestroy( field_list );
   }

   return retval;
}

static int _chatdb_build_insert(
   bstring* query_p, const_bstring fields, const char* table_name
) {
   int retval = 0;
   struct bstrList* field_list = NULL;
   struct bstrList* key_list = NULL;
   bstring key_str = NULL;
   int sep_pos = 0;
   size_t i = 0;
   const struct tagbstring cs_primary = bsStatic( "primary key" );
   const struct tagbstring cs_no_insert = bsStatic( "*-1$" );
   struct bstrList* fmt_list = NULL;
   bstring fmt_str = NULL;
   bstring primary_key = NULL;

   retval = _chatdb_build_split_fields( &field_list, fields );
   if( retval ) {
      goto cleanup;
   }

   /* Create a list to hold truncated keys to used fields. */
   key_list = bstrListCreate();
   bcgi_check_null( key_list );
   retval = bstrListAlloc( key_list, field_list->qty );
   bcgi_check_bstr_err( key_list );

   /* Create a string list to hold format string tokens. */
   fmt_list = bstrListCreate();
   bcgi_check_null( fmt_list );
   retval = bstrListAlloc( fmt_list, field_list->qty );
   bcgi_check_bstr_err( fmt_list );

   for( i = 0 ; field_list->qty > i ; i++ ) {
      assert( NULL != field_list->entry[i]->data );

      sep_pos = bstrchr( field_list->entry[i], '*' );
      assert( BSTR_ERR != sep_pos );
      
      /* Check for valid field types and build fmt_list if valid. */
      if(
         BSTR_ERR != binstrcaseless(
            field_list->entry[i], sep_pos, &cs_primary )
      ) {
         dbglog_debug( 1, "skipping primary key: %s\n",
            bdata( field_list->entry[i] ) );
         primary_key = bmidstr( field_list->entry[i], 0, sep_pos );
         bcgi_check_null( primary_key );
         continue;

      } else if(
         BSTR_ERR != binstr( field_list->entry[i], sep_pos, &cs_no_insert )
      ) {
         dbglog_debug( 1, "skipping no-insert field: %s\n",
            bdata( field_list->entry[i] ) );
         continue;
      } else {
         fmt_list->entry[fmt_list->qty++] = bfromcstr( "?" );
      }

      /* Truncate properties after star separators. */
      key_list->entry[key_list->qty] = bmidstr(
         field_list->entry[i], 0, sep_pos );
      bcgi_check_null( key_list->entry[key_list->qty] );

      key_list->qty++;
   }

   bcgi_check_null( primary_key );
   
   key_str = bjoinStatic( key_list, ", " );
   fmt_str = bjoinStatic( fmt_list, ", " );

   /* Build the query string. */
   if( NULL == *query_p ) {
      *query_p = bfromcstr( "" );
      bcgi_check_null( *query_p );
   }
   retval = bassignformat( *query_p, "insert into users ( %s ) values ( %s )",
      bdata( key_str ), bdata( fmt_str ) );
   bcgi_check_bstr_err( *query_p );

cleanup:

   bcgi_cleanup_bstr( fmt_str, likely );
   bcgi_cleanup_bstr( key_str, likely );
   bcgi_cleanup_bstr( primary_key, likely );

   if( NULL != fmt_list ) {
      bstrListDestroy( fmt_list );
   }

   if( NULL != key_list ) {
      bstrListDestroy( key_list );
   }

   if( NULL != field_list ) {
      bstrListDestroy( field_list );
   }

   return retval;
}

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

   /* XXX */
   /*
   bstring query_b = NULL;
   _chatdb_build_create_table( &query_b, &_gc_chatdb_fields_users, "users" );
   _chatdb_build_insert( &query_b, &_gc_chatdb_fields_users, "users" );
   dbglog_debug( 1, "insert query: %s\n", bdata( query_b ) );
   exit( 1 );
   */

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

   /* Create user table if it doesn't exist. */
   retval = _chatdb_build_create_table(
      &query, &_gc_chatdb_fields_users, "users" );
   if( retval ) {
      goto cleanup;
   }
   pthread_mutex_lock( &(op->db_mutex) );
   retval = sqlite3_exec( op->db, bdata( query ), NULL, 0, &err_msg );
   pthread_mutex_unlock( &(op->db_mutex) );
   bdestroy( query );
   query = NULL;
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
      _chatdb_alter_table( 1, op->db, &(op->db_mutex),
         "alter table users add column session_timeout integer default 3600" );
      /* Fall through. */

   case 1:
      /* Bring up to version one. */
      _chatdb_alter_table( 2, op->db, &(op->db_mutex),
         "alter table users add column flags integer default 0" );
      /* Fall through. */

   default:
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

static int _chatdb_bind_int( sqlite3_stmt* stmt, int* i_p, int value ) {
   int retval = 0;

   assert( NULL != stmt );
   dbglog_debug( 1, "binding value: %d\n", value );
   retval = sqlite3_bind_int( stmt, (*i_p)++, value );
   if( SQLITE_OK != retval ) {
      dbglog_error( "error: %d\n", retval );
      retval = RETVAL_DB;
   }
   
   return retval;
}

static int _chatdb_bind_time_t( sqlite3_stmt* stmt, int* i_p, time_t value ) {
   int retval = 0;
   /* TODO */
   retval = RETVAL_DB;
   return retval;
}

static int _chatdb_bind_bstring( sqlite3_stmt* stmt, int* i_p, bstring value ) {
   int retval = 0;

   assert( NULL != stmt );
   dbglog_debug( 1, "binding value: %s\n", bdata( value ) );
   retval = sqlite3_bind_text(
      stmt, (*i_p)++, bdata( value ), blength( value ), SQLITE_STATIC );
   if( SQLITE_OK != retval ) {
      dbglog_error( "error: %d\n", retval );
      retval = RETVAL_DB;
   }

   return retval;
}

int chatdb_add_user(
   struct CCHAT_OP_DATA* op, struct CHATDB_USER* user, bstring password,
   bstring* err_msg_p
) {
   int retval = 0;
   bstring query_b = NULL;
   char* err_msg = NULL;
   sqlite3_stmt* stmt;
   int i = 0;

   if( 0 == blength( user->user_name ) ) {
      *err_msg_p = bfromcstr( "Username cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   if( 0 >= user->user_id && 0 == blength( password ) ) {
      *err_msg_p = bfromcstr( "New password cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   if( 0 >= user->user_id || 0 < blength( password ) ) {
      dbglog_debug( 1, "changing user password...\n" );

      user->hash_sz = CHATDB_HASH_SZ;
      user->iters = CHATDB_PASSWORD_ITER;
   
      /* Generate a new salt. */
      retval = bcgi_generate_salt( &(user->salt), CHATDB_SALT_SZ );

      /* Hash the provided password. */
      retval = bcgi_hash_password(
         password, user->iters, user->hash_sz, user->salt, &(user->hash) );
      if( retval ) {
         goto cleanup;
      }
   }

   /* This is used to define the inserted/updated fields for a bind or insert.
    */
   #define CHATDB_USER_TABLE_PREPARE_IDX( idx, u, field, c_type, db_type ) \
      if( 0 < u ) { \
         CHATDB_USER_TABLE_PREPARE( idx, u, field, c_type, db_type ); \
      }

   pthread_mutex_lock( &(op->db_mutex) );

   if( 0 < user->user_id ) {
      /* User exists/has an ID; prepare an UPDATE statement. */
      _chatdb_build_update( &query_b, &_gc_chatdb_fields_users, "users" );

      dbglog_debug( 1, "stmt: %s\n", bdata( query_b ) );

      retval = sqlite3_prepare_v2(
         op->db, bdata( query_b ), blength( query_b ), &stmt, NULL );
      if( SQLITE_OK != retval ) {
         dbglog_error( "error during preparation: %s\n",
            sqlite3_errmsg( op->db ) );
         pthread_mutex_unlock( &(op->db_mutex) );
         retval = RETVAL_DB;
         goto cleanup;
      }

      /* Bind the fields with non-zero (/non-1) bind indexes. */
      i = 1;
      CHATDB_USER_TABLE( CHATDB_USER_TABLE_PREPARE_IDX );

      /* Bind the primary key as criteria. */

      #define CHATDB_USER_TABLE_PREPARE_PK( idx, u, field, c_type, db_type ) \
         if( NULL != strstr( db_type, "primary key" ) ) { \
            CHATDB_USER_TABLE_PREPARE( idx, u, field, c_type, db_type ); \
         }

      CHATDB_USER_TABLE( CHATDB_USER_TABLE_PREPARE_PK );

   } else {
      /* No user ID/new ser; prepare an INSERT statement. */
      _chatdb_build_insert( &query_b, &_gc_chatdb_fields_users, "users" );

      dbglog_debug( 1, "stmt: %s\n", bdata( query_b ) );

      retval = sqlite3_prepare_v2(
         op->db, bdata( query_b ), blength( query_b ), &stmt, NULL );
      if( SQLITE_OK != retval ) {
         dbglog_error( "error during preparation: %s\n",
            sqlite3_errmsg( op->db ) );
         pthread_mutex_unlock( &(op->db_mutex) );
         retval = RETVAL_DB;
         goto cleanup;
      }

      /* Bind the fields with non-zero (/non-1) bind indexes. */
      i = 1;
      CHATDB_USER_TABLE( CHATDB_USER_TABLE_PREPARE_IDX );
   }

   /* Execute the statement. */
   retval = sqlite3_step( stmt );
   if( SQLITE_DONE != retval ) {
      dbglog_error( "error %d during step: %s\n",
         retval, sqlite3_errmsg( op->db ) );
      pthread_mutex_unlock( &(op->db_mutex) );
      sqlite3_finalize( stmt );
      retval = RETVAL_DB;
      goto cleanup;
   }
   retval = 0;

   /* Clean up. */
   sqlite3_finalize( stmt );
   pthread_mutex_unlock( &(op->db_mutex) );

cleanup:

   bcgi_cleanup_bstr( query_b, likely );

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

   assert( NULL != in );

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

   #define CHATDB_USER_TABLE_ASSIGN( idx, u, field, c_type, db_type ) \
      chatdb_assign_ ## c_type ( &(arg_struct->user->field), argv[idx] );

   CHATDB_USER_TABLE( CHATDB_USER_TABLE_ASSIGN );

   if( NULL != arg_struct->cb_user ) {
      retval = arg_struct->cb_user(
         arg_struct->page,
         arg_struct->op,
         arg_struct->password_test,
         arg_struct->user );
   }

cleanup:

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

   #define CHATDB_USER_TABLE_SELECT( idx, u, field, c_type, db_type ) \
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

   dbglog_debug( 0, "query: %s\n", query );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   pthread_mutex_lock( &(op->db_mutex) );
   dbglog_debug( 1, "locked\n" );
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


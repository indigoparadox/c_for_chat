
#include "chatdb.h"

#include "bstrlib.h"

#include <stdlib.h> /* for atoi() */

int chatdb_init( bstring path, sqlite3** db_p ) {
   int retval = 0;
   char* err_msg = NULL;

   retval = sqlite3_open( bdata( path ), db_p );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* Create message table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists messages( "
         "msg_id int primary key,"
         "msg_type int not null,"
         "user_from_id int not null,"
         "room_or_user_to_id int not null,"
         "msg_text text,"
         "msg_time datetime default current_timestamp );", NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists chat_schema( version int );",
      NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   return retval;
}

void chatdb_close( sqlite3** db_p ) {

   sqlite3_close( *db_p );
   *db_p = NULL;

}

int chatdb_send_message( sqlite3* db, bstring msg ) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   /* TODO: Actual to/from. */
   query = sqlite3_mprintf(
      "insert into messages values(NULL, 0, 0, 0, '%q', NULL)",
      bdata( msg ) );
   if( NULL == query ) {
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db,
      "select * from messages", NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
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
   chatdb_iter_cb_t cb = (chatdb_iter_cb_t)arg;
   int retval = 0;
   bstring msg_text = NULL;

   msg_text = bfromcstr( argv[4] );
   if( NULL == msg_text ) { 
      goto cleanup;
   }

   retval = cb(
      atoi( argv[0] ), /* msg_id */
      atoi( argv[1] ), /* msg_type */
      atoi( argv[2] ), /* user_from_id */
      atoi( argv[3] ), /* room_or_user_to_id */
      msg_text );

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   return retval;
}

int chatdb_iter_messages(
   sqlite3* db, int msg_type, int dest_id, chatdb_iter_cb_t cb
) {
   int retval = 0;
   char* err_msg = NULL;

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( db,
      "select * from messages", chatdb_dbcb_messages, cb, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   return retval;
}



#include "chatdb.h"

#include "bstrlib.h"

#include <stdlib.h> /* for atoi() */

struct CHATDB_ARG {
   chatdb_iter_cb_t cb;
   FCGX_Request* req;
};

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
         "msg_id integer primary key,"
         "msg_type integer not null,"
         "user_from_id integer not null,"
         "room_or_user_to_id integer not null,"
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

int chatdb_send_message( sqlite3* db, bstring msg, bstring* err_msg_p ) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   /* TODO: Actual to/from. */
   query = sqlite3_mprintf(
      "insert into messages "
      "(msg_type, user_from_id, room_or_user_to_id, msg_text) "
      "values(0, 0, 0, '%q')",
      bdata( msg ) );
   if( NULL == query ) {
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      /* retval = RETVAL_DB; */
      retval = 0;
      *err_msg_p = bfromcstr( err_msg );
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
   bstring msg_text = NULL;

   msg_text = bfromcstr( argv[4] );
   if( NULL == msg_text ) { 
      goto cleanup;
   }

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   retval = arg_struct->cb(
      arg_struct->req,
      atoi( argv[0] ), /* msg_id */
      atoi( argv[1] ), /* msg_type */
      atoi( argv[2] ), /* user_from_id */
      atoi( argv[3] ), /* room_or_user_to_id */
      msg_text,
      atoi( argv[5] ) ); /* msg_time */

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   return retval;
}

int chatdb_iter_messages(
   FCGX_Request* req, sqlite3* db,
   int msg_type, int dest_id, chatdb_iter_cb_t cb
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;

   arg_struct.cb = cb;
   arg_struct.req = req;

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( db,
      "select msg_id, msg_type, user_from_id, room_or_user_to_id, "
         "msg_text, strftime('%s', msg_time) from messages",
      chatdb_dbcb_messages, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      FCGX_FPrintF( req->out, "<tr><td>%s</td><td></td></tr>", err_msg );
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}


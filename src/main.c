

#define DBGLOG_C
#include "main.h"

#include <string.h> /* for memset() */

#include <curl/curl.h>

int main() {
   FCGX_Request req;
   int cgi_sock = -1;
   int retval = 0;
   sqlite3* db = NULL;
   struct tagbstring chatdb_path = bsStatic( "chat.db" );

   retval = dbglog_init( "cchat.log" );
   if( retval ) {
      goto cleanup;
   }

   assert( NULL != g_dbglog_file );

   dbglog_debug( 1, "initializing...\n" );

   curl_global_init( CURL_GLOBAL_ALL );

   retval = chatdb_init( &chatdb_path, &db );
   if( retval ) {
      goto cleanup;
   }

   FCGX_Init();
   memset( &req, 0, sizeof( FCGX_Request ) );

   cgi_sock = FCGX_OpenSocket( "127.0.0.1:9000", 100 );
   FCGX_InitRequest( &req, cgi_sock, 0 );
	while( 0 <= FCGX_Accept_r( &req ) ) {

      retval = cchat_handle_req( &req, db );
      if( RETVAL_ALLOC == retval ) {
         /* Can't fix that. */
         goto cleanup;
      }
   }

cleanup:

   if( 0 <= cgi_sock ) {
      FCGX_Free( &req, cgi_sock );
   }

   if( NULL != db ) {
      chatdb_close( &db );
   }

   curl_global_cleanup();

   dbglog_shutdown();

   return retval;
}


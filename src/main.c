

#define DBGLOG_C
#include "main.h"

#include <string.h> /* for memset() */
#include <stdlib.h> /* for atoi() */
#include <unistd.h> /* for getopt() */

#include <curl/curl.h>

extern bstring g_recaptcha_site_key;
extern bstring g_recaptcha_secret_key;

int main( int argc, char* argv[] ) {
   FCGX_Request req;
   int cgi_sock = -1;
   int retval = 0;
   sqlite3* db = NULL;
   struct tagbstring chatdb_path = bsStatic( "chat.db" );
   int o = 0;
   bstring log_path = NULL;
   bstring server_listen = NULL;

   /* Parse args. */
   while( -1 != (o = getopt( argc, argv, "l:d:s:" )) ) {
      switch( o ) {
      case 'l':
         if( NULL != log_path ) {
            retval = RETVAL_PARAMS;
            goto cleanup;
         }
         log_path = bfromcstr( optarg );
         bcgi_check_null( log_path );
         break;

      case 'd':
         g_dbglog_level = atoi( optarg );
         break;

      case 's':
         if( NULL != server_listen ) {
            retval = RETVAL_PARAMS;
            goto cleanup;
         }
         server_listen = bfromcstr( optarg );
         bcgi_check_null( server_listen );
         break;
      }
   }

   /* Set default log path and start logger. */
   if( NULL == log_path ) {
      log_path = bfromcstr( "cchat.log" );
      bcgi_check_null( log_path );
   }

   retval = dbglog_init( bdata( log_path ) );
   if( retval ) {
      goto cleanup;
   }
   assert( NULL != g_dbglog_file );

   dbglog_debug( g_dbglog_level, "initializing (log level %d)...\n",
      g_dbglog_level );

   /* Set default listen address. */
   if( NULL == server_listen ) {
      server_listen = bfromcstr( "127.0.0.1:9000" );
      bcgi_check_null( server_listen );
   }

   dbglog_debug( 3, "checking environment...\n" );
   g_recaptcha_site_key = bfromcstr( getenv( "CCHAT_RECAPTCHA_SITE" ) );
   if( NULL == g_recaptcha_site_key ) {
      dbglog_debug( 1, "no ReCAPTCHA site key defined...\n" );
   }
   g_recaptcha_secret_key = bfromcstr( getenv( "CCHAT_RECAPTCHA_SECRET" ) );
   if( NULL == g_recaptcha_site_key ) {
      dbglog_debug( 1, "no ReCAPTCHA secret key defined...\n" );
   }

   dbglog_debug( 3, "initializing curl...\n" );

   curl_global_init( CURL_GLOBAL_ALL );

   dbglog_debug( 3, "initializing database...\n" );

   retval = chatdb_init( &chatdb_path, &db );
   if( retval ) {
      goto cleanup;
   }

   dbglog_debug( 3, "initializing FastCGI...\n" );

   FCGX_Init();
   memset( &req, 0, sizeof( FCGX_Request ) );

#ifdef USE_WEBSOCKETS
   dbglog_debug( 3, "starting socket server...\n" );
   retval = socksrv_listen();
   if( retval ) {
      goto cleanup;
   }
#endif /* USE_WEBSOCKETS */

   dbglog_debug( 4, "listening on %s...\n", bdata( server_listen ) );

   cgi_sock = FCGX_OpenSocket( bdata( server_listen ), 100 );
   FCGX_InitRequest( &req, cgi_sock, 0 );
	while( 0 <= FCGX_Accept_r( &req ) ) {

      dbglog_debug( 1, "received request...\n" );

      retval = cchat_handle_req( &req, db );
      if( RETVAL_ALLOC == retval ) {
         /* Can't fix that. */
         goto cleanup;
      } else if( retval ) {
         dbglog_error( "error handling request: %d\n", retval );
      }
   }

cleanup:

   bcgi_cleanup_bstr( server_listen, likely );
   bcgi_cleanup_bstr( log_path, likely );
   bcgi_cleanup_bstr( g_recaptcha_site_key, likely );
   bcgi_cleanup_bstr( g_recaptcha_secret_key, likely );

   if( 0 <= cgi_sock ) {
      FCGX_Free( &req, cgi_sock );
   }

   if( NULL != db ) {
      chatdb_close( &db );
   }

#ifdef USE_WEBSOCKETS
   socksrv_stop();
#endif /* USE_WEBSOCKETS */

   curl_global_cleanup();

   dbglog_shutdown();

   return retval;
}


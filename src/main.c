

#define DBGLOG_C
#include "main.h"

#include <string.h> /* for memset() */
#include <stdlib.h> /* for atoi() */
#include <unistd.h> /* for getopt() */
#include <signal.h>

#include <pthread.h>
#include <curl/curl.h>

extern bstring g_recaptcha_site_key;
extern bstring g_recaptcha_secret_key;

static int g_cgi_sock = -1;
FCGX_Request g_req;
sqlite3* g_db = NULL;
struct lws_context* g_lws_ctx = NULL;
int g_lws_running = 1;

enum lws_protocol_list {
	PROTOCOL_HTTP = 0,
	PROTOCOL_CCHAT,
};

#define main_http_get_header( buffer, wsi, header ) \
   assert( NULL == buffer ); \
   buffer = bfromcstralloc( lws_hdr_total_length( wsi, header ) + 1, "" ); \
   bcgi_check_null( buffer ); \
   lws_hdr_copy( wsi, (char*)((buffer)->data), (buffer)->mlen, header ); \
   buffer->slen = lws_hdr_total_length( wsi, header ); \
   if( ':' == bchare( buffer, 0, ' ' ) ) { \
      /* libws seems to be a bit sloppy about removing colons. */ \
      buffer->data[0] = ' '; \
      retval = btrimws( buffer ); \
      bcgi_check_bstr_err( buffer ); \
   }

static 
int main_cb_http(
   struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
   size_t len
) {
   int retval = 0;
   bstring cookies = NULL;
   dbglog_debug( 1, "cb_http called: %d\n", reason );
   bstring session = NULL;
   int auth_user_id = -1;
   bstring remote_host = NULL; /* TODO */

   switch( reason ) {
   case LWS_CALLBACK_HTTP:
      break;

   case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
      if( 1 >= lws_hdr_total_length( wsi, WSI_TOKEN_HTTP_COOKIE ) ) {
         /* Reject connections with no cookies! */
         retval = RETVAL_AUTH;
         goto cleanup;
      }

      /* Allocate a cookie buffer. */
      main_http_get_header( cookies, wsi, WSI_TOKEN_HTTP_COOKIE );
      main_http_get_header( remote_host, wsi, WSI_TOKEN_X_FORWARDED_FOR );

      /*
      remote_host = bfromcstralloc( 16, "" );
      bcgi_check_null( remote_host );
      lws_get_peer_simple( wsi, (char*)remote_host->data, 16 );
      remote_host->slen = strlen( (char*)remote_host->data );
      */

      dbglog_debug( 1,
         "websocket remote host: \"%s\"\n", bdata( remote_host ) );

      /* See if a valid session exists (don't urldecode!). */
      retval = bcgi_query_key_str( cookies, ';', "session", &session );
      if( retval || NULL == session ) {
         dbglog_debug( 1, "no session cookie found for websocket!\n" );
         retval = RETVAL_AUTH;
         goto cleanup;
      }

      /* Validate session with the database. */
      /* TODO: Database mutex! */
      dbglog_debug( 2, "session cookie found: %s\n", bdata( session ) );
      chatdb_iter_sessions(
         NULL, &auth_user_id, g_db, session,
         remote_host, cchat_auth_session_cb, NULL );

      if( 0 > auth_user_id ) {
         dbglog_debug( 1, "invalid websocket user: %d\n", auth_user_id );
         retval = RETVAL_AUTH;
      }

      break;

   default:
      break;
   }

cleanup:

   bcgi_cleanup_bstr( cookies, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( remote_host, likely );

	return retval;
}

struct lws_protocols lws_protocol_info[] = {
   { "http-only", main_cb_http, 0, 0 },
   { NULL, NULL, 0, 0 }
};

void main_shutdown( int sig ) {

   dbglog_debug( 1, "shutting down...\n" );

   if( NULL != g_db ) {
      dbglog_debug( 1, "shutting down database...\n" );
      chatdb_close( &g_db );
   }

   dbglog_debug( 1, "shutting down socket server...\n" );
   lws_context_destroy( g_lws_ctx );

   if( 0 <= g_cgi_sock ) {
      dbglog_debug( 1, "shutting down FastCGI...\n" );
      FCGX_Free( &g_req, g_cgi_sock );
   }

   dbglog_debug( 1, "shutting down curl...\n" );
   curl_global_cleanup();

   dbglog_debug( 1, "shutting down logging...\n" );
   dbglog_shutdown();

   exit( 0 );
}

static void* main_lws_handler( void* v_ctx ) {

   while( g_lws_running ) {
      lws_service( g_lws_ctx, 10000 );
   }

   return NULL;
}

int main( int argc, char* argv[] ) {
   int retval = 0;
   struct tagbstring chatdb_path = bsStatic( "chat.db" );
   int o = 0;
   bstring log_path = NULL;
   bstring server_listen = NULL;
   struct lws_context_creation_info lws_info;
   pthread_t sock_thd = -1;

   memset( &lws_info, 0, sizeof( struct lws_context_creation_info ) );
   lws_info.gid = -1;
   lws_info.uid = -1;
   lws_info.port = 9777;
   lws_info.protocols = lws_protocol_info;

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

      case 'w':
         lws_info.port = atoi( optarg );
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

   retval = chatdb_init( &chatdb_path, &g_db );
   if( retval ) {
      goto cleanup;
   }

   dbglog_debug( 3, "initializing FastCGI...\n" );

   FCGX_Init();
   memset( &g_req, 0, sizeof( FCGX_Request ) );

   dbglog_debug( 3, "starting socket server...\n" );

   g_lws_ctx = lws_create_context( &lws_info );

   if( pthread_create( &sock_thd, NULL, main_lws_handler, g_lws_ctx ) ) {
      dbglog_error( "error creating client socket thread!\n" );
      /* TODO: Close client socket? */
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   dbglog_debug( 4, "listening on %s...\n", bdata( server_listen ) );

   g_cgi_sock = FCGX_OpenSocket( bdata( server_listen ), 100 );
   FCGX_InitRequest( &g_req, g_cgi_sock, 0 );

   signal( SIGINT, main_shutdown );

	while( 0 <= FCGX_Accept_r( &g_req ) ) {

      dbglog_debug( 1, "received request...\n" );

      retval = cchat_handle_req( &g_req, g_db );
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

   main_shutdown( 0 );

   return retval;
}




#define DBGLOG_C
#include "main.h"

#include <string.h> /* for memset() */
#include <stdlib.h> /* for atoi() */
#include <unistd.h> /* for getopt() */
#include <signal.h>

#include <curl/curl.h>

extern bstring g_recaptcha_site_key;
extern bstring g_recaptcha_secret_key;

static int g_cgi_sock = -1;
struct lws_context* g_lws_ctx = NULL;
int g_lws_running = 1;
struct CCHAT_OP_DATA* g_op = NULL;

enum lws_protocol_list {
	PROTOCOL_HTTP = 0,
	PROTOCOL_CCHAT,
};

static struct RTPROTO_CLIENT* main_lws_client_get(
   struct CCHAT_OP_DATA* op, struct lws* wsi
) {
   struct RTPROTO_CLIENT* out = NULL;
   size_t i = 0;
   
   for( i = 0 ; op->clients_sz > i ; i++ ) {
      if( wsi == op->clients[i].wsi ) {
         out = &(op->clients[i]);
         break;
      }
   }

   return out;
}

int main_lws_client_delete( struct CCHAT_OP_DATA* op, struct lws* wsi ) {
   int retval = 0;
   size_t i = 0;

   for( i = 0 ; op->clients_sz > i ; i++ ) {
      if( wsi != op->clients[i].wsi ) {
         continue;
      }

      retval = rtproto_client_delete( op, i );
      break;
   }

   return retval;
}

int main_lws_client_add(
   struct CCHAT_OP_DATA* op, struct lws* wsi, int auth_user_id
) {
   int retval = 0;
   ssize_t idx_new = -1;

   retval = rtproto_client_add( op, auth_user_id, &idx_new );
   if( retval || 0 > idx_new ) {
      goto cleanup;
   }

   op->clients[idx_new].wsi = wsi;

cleanup:

   return retval;
}

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
int main_cb_cchat(
   struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
   size_t len
) {
   int retval = 0;
   bstring cookies = NULL;
   bstring session = NULL;
   bstring line = NULL;
   int auth_user_id = -1;
   bstring remote_host = NULL; /* TODO */
   struct CCHAT_OP_DATA* op = NULL;
   struct lws_context* ctx = NULL;
   struct RTPROTO_CLIENT* client = NULL;

   dbglog_debug( 1, "cb_cchat called: %d\n", reason );

   ctx = lws_get_context( wsi );
   op = lws_context_user( ctx );
   assert( NULL != op );

   switch( reason ) {
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
      dbglog_debug( 2, "session cookie found: %s\n", bdata( session ) );
      chatdb_iter_sessions(
         NULL, &auth_user_id, op, session,
         remote_host, cchat_auth_session_cb, NULL );

      if( 0 > auth_user_id ) {
         dbglog_debug( 1, "invalid websocket user: %d\n", auth_user_id );
         retval = RETVAL_AUTH;
      }

      retval = main_lws_client_add( op, wsi, auth_user_id );

      break;

   case LWS_CALLBACK_RECEIVE:
      client = main_lws_client_get( op, wsi );
      bcgi_check_null( client );
      line = blk2bstr( in, len );
      bcgi_check_null( line );
      rtproto_command( op, client->auth_user_id, line );
      break;

   case LWS_CALLBACK_SERVER_WRITEABLE:
      /* Get the client. */
      client = main_lws_client_get( op, wsi );
      bcgi_check_null( client );
      assert( NULL != client->buffer );

      assert( NULL == line );
      line = bfromcstralloc( blength( client->buffer ) + LWS_PRE, "" );
      bcgi_check_null( line );
      strncpy(
         (char*)&(line->data[LWS_PRE]),
         (char*)(client->buffer->data), blength( client->buffer ) );
      line->slen = LWS_PRE + blength( client->buffer );

      lws_write( wsi,
         (unsigned char*)&(line->data[LWS_PRE]), line->slen - LWS_PRE,
         LWS_WRITE_TEXT );
      break;

   case LWS_CALLBACK_CLOSED:
      main_lws_client_delete( op, wsi );
      break;

   default:
      break;
   }

cleanup:

   dbglog_debug( 1, "callback complete!\n" );

   bcgi_cleanup_bstr( cookies, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( remote_host, likely );
   bcgi_cleanup_bstr( line, likely );

   return retval;
}

static 
int main_cb_http(
   struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
   size_t len
) {
   int retval = 0;

   dbglog_debug( 1, "cb_http called: %d\n", reason );

   switch( reason ) {
   case LWS_CALLBACK_HTTP:
      break;

   default:
      break;
   }

	return retval;
}

struct lws_protocols lws_protocol_info[] = {
   { "http-only", main_cb_http, 0, 0 },
   { "cchat-protocol", main_cb_cchat, 0, 0 },
   { NULL, NULL, 0, 0 }
};

void main_shutdown( int sig ) {

   dbglog_debug( 1, "shutting down...\n" );

   if( NULL == g_op ) {
      goto no_g_op;
   }

   if( NULL != g_op->db ) {
      dbglog_debug( 1, "shutting down database...\n" );
      chatdb_close( g_op );
   }

   dbglog_debug( 1, "shutting down socket server...\n" );
   lws_context_destroy( g_lws_ctx );

   if( NULL != g_op->clients ) {
      while( 0 < g_op->clients_sz ) {
         rtproto_client_delete( g_op, 0 );
      }
      free( g_op->clients );
      g_op->clients = NULL;
   }

   if( 0 <= g_cgi_sock ) {
      dbglog_debug( 1, "shutting down FastCGI...\n" );
      FCGX_Free( &(g_op->req), g_cgi_sock );
   }

   dbglog_debug( 1, "shutting down curl...\n" );
   curl_global_cleanup();

   dbglog_debug( 1, "shutting down logging...\n" );
   dbglog_shutdown();

no_g_op:

   if( NULL != g_op ) {
      free( g_op );
   }

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
   pthread_t sock_thd;

   g_op = calloc( sizeof( struct CCHAT_OP_DATA ), 1 );
   bcgi_check_null( g_op );

   memset( &lws_info, 0, sizeof( struct lws_context_creation_info ) );
   lws_info.gid = -1;
   lws_info.uid = -1;
   lws_info.port = 9777;
   lws_info.protocols = lws_protocol_info;
   lws_info.user = g_op;

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

   retval = chatdb_init( &chatdb_path, g_op );
   if( retval ) {
      goto cleanup;
   }

   dbglog_debug( 3, "initializing FastCGI...\n" );

   FCGX_Init();
   memset( &(g_op->req), 0, sizeof( FCGX_Request ) );

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
   FCGX_InitRequest( &(g_op->req), g_cgi_sock, 0 );

   signal( SIGINT, main_shutdown );

	while( 0 <= FCGX_Accept_r( &(g_op->req) ) ) {

      dbglog_debug( 1, "received request...\n" );

      retval = cchat_handle_req( g_op );
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



#include "main.h"

#include <stdlib.h> /* for calloc() */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

static pthread_t g_server_thd = 0;
static struct SOCKSRV_SERVER g_socksrv_srv;

int socksrv_get_header(
   bstring key, bstring* value_p, struct SOCKSRV_HEADERS* h
) {
   int retval = 0;
   size_t i = 0;

   assert( h->keys->qty == h->values->qty );
   assert( NULL == *value_p );

   for( i = 0 ; h->keys->qty > i ; i++ ) {
      if( 0 == bstrcmp( key, h->keys->entry[i] ) ) {
         *value_p = bstrcpy( h->values->entry[i] );
         bcgi_check_null( *value_p );
         break;
      }
   }

cleanup:

   return retval;
}

int socksrv_parse_headers( bstring buffer, struct SOCKSRV_HEADERS* h ) {
   int retval = 0;
   int sep_pos = 0;
   size_t i_in = 0;
   struct bstrList* lines = NULL;
   struct tagbstring newline = bsStatic( "\r\n" );

   lines = bsplitstr( buffer, &newline );
   bcgi_check_null( lines );

   assert( NULL == h->keys );
   assert( NULL == h->values );

   /* Allocate key/value lists. */
   h->keys = bstrListCreate();
   bcgi_check_null( h->keys );
   retval = bstrListAlloc( h->keys, lines->qty );
   bcgi_check_bstr_err( h->keys );

   h->values = bstrListCreate();
   bcgi_check_null( h->values );
   retval = bstrListAlloc( h->values, lines->qty );
   bcgi_check_bstr_err( h->values );

   for( i_in = 0 ; lines->qty > i_in ; i_in++ ) {
      (h->values)->entry[(h->values)->qty] = NULL;
      (h->keys)->entry[(h->keys)->qty] = NULL;
      dbglog_debug( 1, "line: \"%s\"\n", bdata( lines->entry[i_in] ) );
      sep_pos = bstrchr( lines->entry[i_in], ':' );
      if( BSTR_ERR == sep_pos ) {
         dbglog_error( "error parsing header %d!\n", i_in );
         continue;
      }

      /* Cut out the header value. */
      (h->values)->entry[(h->values)->qty] = bmidstr(
         lines->entry[i_in], sep_pos + 1,
         blength( lines->entry[i_in] ) - (sep_pos + 1) );
      bcgi_check_null( (h->values)->entry[(h->values)->qty] );

      retval = btrimws( (h->values)->entry[(h->values)->qty] );
      bcgi_check_bstr_err( (h->values)->entry[(h->values)->qty] );

      /* Only increment if successful! */
      (h->values)->qty++;
      
      /* Cut out the header key. */
      (h->keys)->entry[(h->keys)->qty] = bmidstr(
         lines->entry[i_in], 0, sep_pos );
      bcgi_check_null( (h->keys)->entry[(h->keys)->qty] )

      retval = btrimws( (h->keys)->entry[(h->keys)->qty] );
      bcgi_check_bstr_err( (h->keys)->entry[(h->keys)->qty] );

      /* Only increment if successful! */
      (h->keys)->qty++;
   }

   dbglog_debug( 1, "parsed headers!\n" );

cleanup:

   return retval;
}

static
int _socksrv_client_handshake( struct SOCKSRV_SERVER* srv, bstring buffer ) {
   int retval = 0;
   struct tagbstring bs_secsoc = bsStatic( "Sec-WebSocket-Key" );
   bstring secsoc = NULL;
   size_t i = 0;
   bstring secsoc_reply_hash = NULL;
   bstring secsoc_reply = NULL;
   size_t sent = 0;

   dbglog_debug( 1, "starting websocket handshake for socket: %d\n",
      srv->client );

   socksrv_parse_headers( buffer, &(srv->h) );
   bcgi_check_null( srv->h.keys );
   bcgi_check_null( srv->h.values );

   for( i = 0 ; srv->h.keys->qty > i ; i++ ) {
      dbglog_debug( 1, "header key: \"%s\", value: \"%s\"\n",
         bdata( srv->h.keys->entry[i] ), bdata( srv->h.values->entry[i] ) );
   }

   retval = socksrv_get_header( &bs_secsoc, &secsoc, &(srv->h) );
   dbglog_debug( 1, "socket security key: %s\n", bdata( secsoc ) );

   retval = bcatcstr( secsoc, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" );
   bcgi_check_bstr_err( secsoc );

   dbglog_debug( 1, "security key return hash of plain: %s\n",
      bdata( secsoc ) );

   /* XXX */
   retval = bcgi_hash_sha( secsoc, &secsoc_reply_hash );

   secsoc_reply = bformat(
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connecttion: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n",
      bdata( secsoc_reply_hash ) );
   bcgi_check_null( secsoc_reply );

   sent = send(
      srv->client, bdata( secsoc_reply ), blength( secsoc_reply ), 0 );

   dbglog_debug( 1, "reply: %s", bdata( secsoc_reply ) );

   /* Handshake complete! */
   srv->stage++;

cleanup:

   bcgi_cleanup_bstr( secsoc, likely );
   bcgi_cleanup_bstr( secsoc_reply, likely );
   bcgi_cleanup_bstr( secsoc_reply_hash, likely );

   return retval;
}

static void* _socksrv_client_handler( void* srv_p ) {
   struct SOCKSRV_SERVER* srv = (struct SOCKSRV_SERVER*)srv_p;
   size_t read_sz = 0;
   bstring buffer = NULL;
   int retval = 0;
   char buffer_c[SOCKSRV_BUF_SZ_MAX + 1];

   buffer = bfromcstralloc( SOCKSRV_BUF_SZ_MAX + 1, "" );
   bcgi_check_null( buffer );

   while(
      0 < (read_sz = recv( srv->client, buffer_c, SOCKSRV_BUF_SZ_MAX, 0 ))
   ) {
      bassignblk( buffer, buffer_c, read_sz );
      memset( buffer_c, 0, SOCKSRV_BUF_SZ_MAX );
      dbglog_debug( 1, "client: %s\n", bdata( buffer ) );

      if( NULL != srv->handlers[srv->stage] ) {
         retval = srv->handlers[srv->stage]( srv, buffer );
      }
      
      /* TODO: Validate session and get user. */
   }

cleanup:

   bcgi_cleanup_bstr( buffer, likely );

   if( retval ) {
      dbglog_error( "client socket thread terminated with: %d\n", retval );
   }

   return NULL;
}

static void* _socksrv_listen_handler( void* v_args ) {
   int retval = 0;
   int sock_cli = 0;
   struct sockaddr_in sock_addr_cli;
   socklen_t sock_cli_sz = 0;
   pthread_t cli_thd = 0;
   struct SOCKSRV_SERVER* srv_cli = NULL;

   while( (sock_cli = accept(
      g_socksrv_srv.server, (struct sockaddr*)&sock_addr_cli, &sock_cli_sz
   )) ) {
      dbglog_debug( 1, "client socket accepted: %d\n", sock_cli );

      srv_cli = calloc( sizeof( struct SOCKSRV_SERVER ), 1 );
      bcgi_check_null( srv_cli );
      memcpy( srv_cli, &g_socksrv_srv, sizeof( struct SOCKSRV_SERVER ) );
      /* TODO: Check memcpy! */

      srv_cli->client = sock_cli;

      if( pthread_create(
         /* TODO: Keep cli_thd for later? */
         &cli_thd, NULL, _socksrv_client_handler, srv_cli
      ) ) {
         dbglog_error( "error creating client socket thread!\n" );
         /* TODO: Close client socket? */
         continue;
      }

      /* It's the client's problem now! */
      /* TODO: Keep hold of for later to send messages. */
      srv_cli = NULL;

   }

cleanup:

   return NULL;
}

int socksrv_listen() {
   int retval = 0;
   struct sockaddr_in sock_addr_srv;

   memset( &g_socksrv_srv, '\0', sizeof( struct SOCKSRV_SERVER ) );

   /* Set universal handshake handler. */
   g_socksrv_srv.handlers[0] = _socksrv_client_handshake;

   /* Create the listener socket. */
   g_socksrv_srv.server = socket( AF_INET, SOCK_STREAM, 0 );
   if( 0 > g_socksrv_srv.server ) {
      dbglog_error( "unable to create listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   /* Bind the listener socket. */
   sock_addr_srv.sin_family = AF_INET;
   sock_addr_srv.sin_addr.s_addr = INADDR_ANY; /* TODO: Make configurable. */
   sock_addr_srv.sin_port = htons( 9777 ); /* TODO: Make configurable. */

   if( 0 > bind( g_socksrv_srv.server,
      (struct sockaddr*)&sock_addr_srv, sizeof( sock_addr_srv ) )
   ) {
      dbglog_error( "unable to bind listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   listen( g_socksrv_srv.server, 3 );

   pthread_create( &g_server_thd, NULL, _socksrv_listen_handler, NULL );

   dbglog_debug( 4, "waiting for socket connections...\n" );

cleanup:

   return retval;
}

void socksrv_stop() {
   /* TODO: Close socket. */
   if( 0 <= g_socksrv_srv.server ) {
      close( g_socksrv_srv.server );
      g_socksrv_srv.server = -1;
   }

   /* TODO: Stop thread. */
}


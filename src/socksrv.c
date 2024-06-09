
#include "main.h"

#include <stdlib.h> /* for calloc() */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

struct SOCKSRV_ARGS {
   int sock_srv;
};

static pthread_t g_server_thd = 0;
static int g_sock_srv = -1;

int socksrv_parse_headers(
   bstring buffer, struct bstrList** keys_p, struct bstrList** values_p
) {
   int retval = 0;
   int sep_pos = 0;
   size_t i = 0;
   struct bstrList* lines = NULL;
   struct tagbstring newline = bsStatic( "\r\n" );

   lines = bsplitstr( buffer, &newline );
   bcgi_check_null( lines );

   assert( NULL == *keys_p );
   assert( NULL == *values_p );

   /* Allocate key/value lists. */
   *keys_p = bstrListCreate();
   bcgi_check_null( *keys_p );
   retval = bstrListAlloc( *keys_p, lines->qty );
   bcgi_check_bstr_err( *keys_p );

   *values_p = bstrListCreate();
   bcgi_check_null( *values_p );
   retval = bstrListAlloc( *values_p, lines->qty );
   bcgi_check_bstr_err( *values_p );

   for( i = 0 ; lines->qty > i ; i++ ) {
      (*values_p)->entry[i] = NULL;
      (*keys_p)->entry[i] = NULL;
      dbglog_debug( 1, "line: \"%s\"\n", bdata( lines->entry[i] ) );
      sep_pos = bstrchr( lines->entry[i], ':' );
      if( BSTR_ERR == sep_pos ) {
         dbglog_error( "error parsing header %d!\n", i );
         continue;
      }

      /* Cut out the header value. */
      (*values_p)->entry[i] = bmidstr(
         lines->entry[i], sep_pos + 1,
         blength( lines->entry[i] ) - (sep_pos + 1) );
      bcgi_check_null( (*values_p)->entry[i] );

      retval = btrimws( (*values_p)->entry[i] );
      bcgi_check_bstr_err( (*values_p)->entry[i] );

      /* TODO: Only increment if successful! */
      (*values_p)->qty = i;
      
      /* Cut out the header key. */
      (*keys_p)->entry[i] = bmidstr( lines->entry[i], 0, sep_pos );
      bcgi_check_null( (*keys_p)->entry[i] )

      retval = btrimws( (*keys_p)->entry[i] );
      bcgi_check_bstr_err( (*keys_p)->entry[i] );

      /* TODO: Only increment if successful! */
      (*keys_p)->qty = i;
   }

   dbglog_debug( 1, "parsed headers!\n" );

cleanup:

   return retval;
}

static void* _socksrv_client_handler( void* sock_p ) {
   int sock = *((int*)sock_p);
   size_t read_sz = 0;
   bstring buffer = NULL;
   int retval = 0;
   char buffer_c[SOCKSRV_BUF_SZ_MAX + 1];
   bstring session = NULL;
   struct bstrList* header_keys = NULL;
   struct bstrList* header_values = NULL;
   size_t i = 0;

   buffer = bfromcstralloc( SOCKSRV_BUF_SZ_MAX + 1, "" );
   bcgi_check_null( buffer );

   while( 0 < (read_sz = recv( sock, buffer_c, SOCKSRV_BUF_SZ_MAX, 0 )) ) {
      bassignblk( buffer, buffer_c, read_sz );
      dbglog_debug( 1, "client: %s", bdata( buffer ) );
      
      socksrv_parse_headers( buffer, &header_keys, &header_values );
      bcgi_check_null( header_keys );
      bcgi_check_null( header_values );

      for( i = 0 ; header_keys->qty > i ; i++ ) {
         dbglog_debug( 1, "header key: \"%s\", value: \"%s\"\n",
            bdata( header_keys->entry[i] ), bdata( header_values->entry[i] ) );
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
   int sock_cli = 0;
   struct sockaddr_in sock_addr_cli;
   socklen_t sock_cli_sz = 0;
   pthread_t cli_thd = 0;
   struct SOCKSRV_ARGS* args = (struct SOCKSRV_ARGS*) v_args;

   while( (sock_cli = accept(
      g_sock_srv, (struct sockaddr*)&sock_addr_cli, &sock_cli_sz
   )) ) {
      dbglog_debug( 1, "client socket accepted: %d\n", sock_cli );

      if( pthread_create(
         /* TODO: Keep cli_thd for later? */
         &cli_thd, NULL, _socksrv_client_handler, &sock_cli
      ) ) {
         dbglog_error( "error creating client socket thread!\n" );
         /* TODO: Close client socket? */
         continue;
      }

   }

cleanup:

   return NULL;
}

int socksrv_listen() {
   int retval = 0;
   struct sockaddr_in sock_addr_srv;
   struct SOCKSRV_ARGS* args = NULL;

   args = calloc( sizeof( struct SOCKSRV_ARGS ), 1 );
   bcgi_check_null( args );

   /* Create the listener socket. */
   g_sock_srv = socket( AF_INET, SOCK_STREAM, 0 );
   if( 0 > g_sock_srv ) {
      dbglog_error( "unable to create listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   /* Bind the listener socket. */
   sock_addr_srv.sin_family = AF_INET;
   sock_addr_srv.sin_addr.s_addr = INADDR_ANY; /* TODO: Make configurable. */
   sock_addr_srv.sin_port = htons( 9777 ); /* TODO: Make configurable. */

   if( 0 > bind( g_sock_srv,
      (struct sockaddr*)&sock_addr_srv, sizeof( sock_addr_srv ) )
   ) {
      dbglog_error( "unable to bind listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   listen( g_sock_srv, 3 );

   pthread_create( &g_server_thd, NULL, _socksrv_listen_handler, args );

   dbglog_debug( 4, "waiting for socket connections...\n" );

cleanup:

   return retval;
}

void socksrv_stop() {
   /* TODO: Close socket. */
   if( 0 <= g_sock_srv ) {
      close( g_sock_srv );
      g_sock_srv = -1;
   }

   /* TODO: Stop thread. */
}


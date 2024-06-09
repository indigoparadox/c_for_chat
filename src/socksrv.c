
#include "main.h"

#include <stdlib.h> /* for calloc() */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

struct SOCKSRV_ARGS {
   int sock_srv;
};

static pthread_t g_server_thd = 0;

int socksrv_parse_header( bstring buffer, bstring key, bstring value ) {
   int retval = 0;
   int sep_pos = 0;

   retval = bassigncstr( key, "" );
   bcgi_check_bstr_err( key );
   retval = bassigncstr( value, "" );
   bcgi_check_bstr_err( value );

   sep_pos = bstrchr( buffer, ':' );
   if( BSTR_ERR == sep_pos ) {
      goto cleanup;
   }

   retval = bassignmidstr(
      value, buffer, sep_pos, blength( buffer ) - sep_pos );
   bcgi_check_bstr_err( value );

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

   buffer = bfromcstralloc( SOCKSRV_BUF_SZ_MAX + 1, "" );
   bcgi_check_null( buffer );

   while( 0 < (read_sz = recv( sock, buffer_c, SOCKSRV_BUF_SZ_MAX, 0 )) ) {
      bassignblk( buffer, buffer_c, read_sz );
      dbglog_debug( 1, "client: %s", bdata( buffer ) );

      /* TODO: Validate session and get user. */
   }

cleanup:

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
      args->sock_srv, (struct sockaddr*)&sock_addr_cli, &sock_cli_sz
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
   args->sock_srv = socket( AF_INET, SOCK_STREAM, 0 );
   if( 0 > args->sock_srv ) {
      dbglog_error( "unable to create listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   /* Bind the listener socket. */
   sock_addr_srv.sin_family = AF_INET;
   sock_addr_srv.sin_addr.s_addr = INADDR_ANY; /* TODO: Make configurable. */
   sock_addr_srv.sin_port = htons( 9777 ); /* TODO: Make configurable. */

   if( 0 > bind( args->sock_srv,
      (struct sockaddr*)&sock_addr_srv, sizeof( sock_addr_srv ) )
   ) {
      dbglog_error( "unable to bind listener socket!\n" );
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   listen( args->sock_srv, 3 );

   pthread_create( &g_server_thd, NULL, _socksrv_listen_handler, args );

   dbglog_debug( 4, "waiting for socket connections...\n" );

cleanup:

   return retval;
}

void socksrv_stop() {
   /* TODO: Close socket. */

   /* TODO: Stop thread. */
}


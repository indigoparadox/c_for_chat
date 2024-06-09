
#include "main.h"

#include <stdlib.h> /* for calloc() */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

struct SOCKSRV_ARGS {
   int sock_srv;
};

static pthread_t g_server_thd = 0;

static void* _socksrv_client_handler( void* sock_p ) {
   return NULL;
}

static void* _socksrv_listen_handler( void* v_args ) {
   int sock_cli = 0;
   struct sockaddr_in sock_addr_cli;
   socklen_t sock_cli_sz = 0;
   struct SOCKSRV_ARGS* args = (struct SOCKSRV_ARGS*) v_args;

   while( (sock_cli = accept(
      args->sock_srv, (struct sockaddr*)&sock_addr_cli, &sock_cli_sz
   )) ) {
      dbglog_debug( 1, "client socket accepted: %d\n", sock_cli );


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



#include "main.h"

typedef int (*rtproto_cmd_cb_t)(
   struct CCHAT_OP_DATA* op, int user_id, bstring line );

#define RTPROTO_CMD_TABLE( f ) \
   f( "PRIVMSG", rtproto_cmd_privmsg )

static int rtproto_cmd_privmsg(
   struct CCHAT_OP_DATA* op, int user_id, bstring line
) {
   int retval = 0;
   bstring msg = NULL;
   bstring msg_raw = NULL;
   bstring msg_rt = NULL;
   int msg_pos = 0;
   struct CHATDB_USER user;
   time_t t = 0;

   memset( &user, '\0', sizeof( struct CHATDB_USER ) );

   /* Figure out the sending user. */
   user.user_id = user_id;
   retval = chatdb_iter_users( NULL, op, NULL, &user, NULL, NULL );
   if( retval ) {
      dbglog_error( "error fetching user!\n" );
      goto cleanup;
   }

   /* Cut off the command prefix. */
   msg_pos = bstrchr( line, ':' );
   if( 0 >= msg_pos || blength( line ) <= msg_pos + 1 ) {
      dbglog_debug( 2, "invalid message: %s\n", line );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }
   msg_raw = bmidstr( line, msg_pos + 1, blength( line ) - (msg_pos + 1) );
   bcgi_check_null( msg_raw );
   retval = btrimws( msg_raw );
   bcgi_check_bstr_err( retval );

   if( 0 == blength( msg_raw ) ) {
      goto cleanup;
   }

   /* Sanitize HTML entities from the message before sending out. */
   retval = bcgi_html_escape( msg_raw, &msg );

   /* Prepend destination PRIVMSG command. */
   t = time( NULL );
   msg_rt = bformat(
      ":%s PRIVMSG: %d %s", bdata( user.user_name ), t, bdata( msg ) );
   bcgi_check_null( msg_rt );

   rtproto_client_write_all( op, msg_rt );

   chatdb_send_message( op, user_id, msg, NULL );

cleanup:

   bcgi_cleanup_bstr( msg, likely );
   bcgi_cleanup_bstr( msg_raw, likely );
   bcgi_cleanup_bstr( msg_rt, likely );

   chatdb_free_user( &user );

   return retval;
}

#define RTPROTO_CMD_TABLE_CB( pfx, cb ) cb,

static rtproto_cmd_cb_t gc_rtproto_cmd_cbs[] = {
   RTPROTO_CMD_TABLE( RTPROTO_CMD_TABLE_CB )
   NULL
};

#define RTPROTO_CMD_TABLE_PFX( pfx, cb ) bsStatic( pfx ),

static struct tagbstring gc_rtproto_cmd_pfxs[] = {
   RTPROTO_CMD_TABLE( RTPROTO_CMD_TABLE_PFX )
};

int rtproto_command( struct CCHAT_OP_DATA* op, int user_id, bstring line ) {
   int retval = 0;
   size_t i = 0;

   dbglog_debug( 1, "cmd: %s\n", bdata( line ) );

   while( NULL != gc_rtproto_cmd_cbs[i] ) {
      if( !bstrncmp( line, &(gc_rtproto_cmd_pfxs[i]), bstrchr( line, ' ' ) ) ) {
         retval = gc_rtproto_cmd_cbs[i]( op, user_id, line );
      }

      i++;
   }

   return retval;
}

int rtproto_client_add(
   struct CCHAT_OP_DATA* op, int auth_user_id, ssize_t* out_idx_p
) {
   int retval = 0;
   struct RTPROTO_CLIENT* clients_new = NULL;

   dbglog_debug( 2, "adding client for user: %d\n", auth_user_id );

   pthread_mutex_lock( &(op->clients_mutex) );

   /* Make sure there's room for the new client. */
   if( NULL == op->clients ) {
      op->clients = calloc( sizeof( struct RTPROTO_CLIENT ), 10 );
      op->clients_sz_max = 10;
   } else if( op->clients_sz + 1 >= op->clients_sz_max ) {
      clients_new = realloc( op->clients, op->clients_sz_max * 2 );
      bcgi_check_null( clients_new );
      op->clients = clients_new;
      op->clients_sz_max *= 2;
   }

   dbglog_debug( 1, "creating client %d buffer list...\n", op->clients_sz );
   op->clients[op->clients_sz].buffer = bstrListCreate();
   bcgi_check_null( op->clients[op->clients_sz].buffer );
   retval = bstrListAlloc(
      op->clients[op->clients_sz].buffer, RTPROTO_BUFFER_CT + 1 );
   bcgi_check_bstr_err( key_list );
   op->clients[op->clients_sz].auth_user_id = auth_user_id;
   *out_idx_p = op->clients_sz;
   op->clients_sz++;

   dbglog_debug( 2, "added client: %d\n", *out_idx_p );

cleanup:

   pthread_mutex_unlock( &(op->clients_mutex) );

   return retval;
}

int rtproto_client_write_all( struct CCHAT_OP_DATA* op, bstring buffer ) {
   int retval = 0;
   size_t i = 0;
   int buffer_locked = 0;
   struct RTPROTO_CLIENT* client = NULL;

   dbglog_debug( 1, "locking clients...\n" );
   pthread_mutex_lock( &(op->clients_mutex) );
   for( i = 0 ; op->clients_sz > i ; i++ ) {
      client = &(op->clients[i]);
      dbglog_debug( 1, "locking client %d buffer...\n", i );
      pthread_mutex_lock( &(client->buffer_mutex) );
      buffer_locked = 1;

      dbglog_debug( 1, "writing to client %d...\n", i );

      assert( NULL != client->buffer );
      if( RTPROTO_BUFFER_CT - 1 <= client->buffer->qty ) {
         dbglog_debug( 1, "client %d send buffer full!\n", i );
         pthread_mutex_unlock( &(client->buffer_mutex) );
         dbglog_debug( 1, "unlocked client %d buffer...\n", i );
         buffer_locked = 0;
         continue;
      }
      
      /* Copy the incoming buffer into the client buffer list. */
      client->buffer->entry[client->buffer->qty] = bstrcpy( buffer );
      bcgi_check_null( client->buffer->entry[client->buffer->qty] );
      client->buffer->qty++;

      lws_callback_on_writable( client->wsi );
      pthread_mutex_unlock( &(client->buffer_mutex) );
      dbglog_debug( 1, "unlocked client %d buffer...\n", i );
      buffer_locked = 0;
   }

cleanup:

   if( buffer_locked ) {
      assert( NULL != client );
      pthread_mutex_unlock( &(client->buffer_mutex) );
      dbglog_debug( 1, "unlocked client %d buffer...\n", i );
   }
   pthread_mutex_unlock( &(op->clients_mutex) );
   dbglog_debug( 1, "unlocked clients...\n" );

   return retval;
}

int rtproto_client_delete( struct CCHAT_OP_DATA* op, size_t idx ) {
   int retval = 0;
   size_t i = 0;

   dbglog_debug( 2, "removing client %d...\n", idx );

   pthread_mutex_lock( &(op->clients_mutex) );

   dbglog_debug( 1, "destroying client %d buffer list...\n", idx );
   bstrListDestroy( op->clients[idx].buffer );
   op->clients[idx].buffer = NULL;
   op->clients[idx].wsi = NULL;
   op->clients[idx].auth_user_id = 0;
   
   for( i = idx ; op->clients_sz - 1 > i ; i++ ) {
      memcpy( &(op->clients[i]), &(op->clients[i + 1]),
         sizeof( struct RTPROTO_CLIENT ) );
   }

   op->clients_sz--;

   dbglog_debug( 2, "client %d removed; %d clients remain.\n",
      idx, op->clients_sz );

   pthread_mutex_unlock( &(op->clients_mutex) );

   return retval;
}


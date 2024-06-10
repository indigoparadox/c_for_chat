
#include "main.h"

typedef int (*rtproto_cmd_cb_t)(
   struct CCHAT_OP_DATA* op, int user_id, bstring line );

#define RTPROTO_CMD_TABLE( f ) \
   f( "PRIVMSG", rtproto_cmd_privmsg )

int rtproto_lookup_user_cb(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   bstring password_test, int* user_id_out_p, int* session_timeout_p,
   int user_id, bstring user_name, bstring email,
   bstring hash, size_t hash_sz, bstring salt, size_t iters, time_t msg_time,
   int session_timeout
) {
   int retval = 0;

   *user_id_out_p = user_id;

   return retval;
}

static int rtproto_cmd_privmsg(
   struct CCHAT_OP_DATA* op, int user_id, bstring line
) {
   int retval = 0;
   bstring msg = NULL;
   int msg_pos = 0;

#if 0
   /* Figure out the sending user. */
   retval = chatdb_iter_users(
      NULL, op, NULL, user_id, NULL, NULL,
      NULL, rtproto_lookup_user_cb, NULL );
#endif

   /* Cut off the command prefix. */
   msg_pos = bstrchr( line, ':' );
   if( 0 >= msg_pos || blength( line ) <= msg_pos + 1 ) {
      dbglog_debug( 2, "invalid message: %s\n", line );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }
   msg = bmidstr( line, msg_pos + 1, blength( line ) - (msg_pos + 1) );
   bcgi_check_null( msg );
   retval = btrimws( msg );
   bcgi_check_bstr_err( retval );

   retval = binsertStatic( msg, 0, "XXX_USER", '\0' );
   bcgi_check_bstr_err( msg );

   rtproto_client_write_all( op, msg );

cleanup:

   bcgi_cleanup_bstr( msg, likely );

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

   op->clients[op->clients_sz].buffer = bfromcstr( "" );
   op->clients[op->clients_sz].auth_user_id = auth_user_id;
   *out_idx_p = op->clients_sz;
   op->clients_sz++;

   dbglog_debug( 2, "added client: %d\n", *out_idx_p );

cleanup:

   return retval;
}

int rtproto_client_write_all( struct CCHAT_OP_DATA* op, bstring buffer ) {
   int retval = 0;
   size_t i = 0;

   for( i = 0 ; op->clients_sz > i ; i++ ) {
      retval = bassign( op->clients[i].buffer, buffer );
      bcgi_check_bstr_err( op->clients[i].buffer );
      lws_callback_on_writable( op->clients[i].wsi );
   }

cleanup:

   return retval;
}

int rtproto_client_delete( struct CCHAT_OP_DATA* op, size_t idx ) {
   int retval = 0;
   size_t i = 0;

   dbglog_debug( 2, "removing client %d...\n", idx );

   bdestroy( op->clients[i].buffer );
   op->clients[i].buffer = NULL;
   op->clients[i].wsi = NULL;
   op->clients[i].auth_user_id = 0;
   
   for( i = idx ; op->clients_sz - 1 > i ; i++ ) {
      memcpy( &(op->clients[i]), &(op->clients[i + 1]),
         sizeof( struct RTPROTO_CLIENT ) );
   }

   op->clients_sz--;

   dbglog_debug( 2, "client %d removed; %d clients remain.\n",
      idx, op->clients_sz );

   return retval;
}


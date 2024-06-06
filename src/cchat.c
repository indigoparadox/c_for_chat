
#include "chatdb.h"
#include "cchat.h"

#include <stdlib.h> /* for atoi() */

typedef int (*cchat_route_cb_t)(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db );

#define CCHAT_ROUTES_TABLE( f ) \
   f( "/send", cchat_route_send, "POST" ) \
   f( "/", cchat_route_root, "GET" ) \
   f( "", NULL, "" )

#define CCHAT_CONST_STRS_TABLE( f ) \
   f( "<", "&lt;", CSTR_LT ) \
   f( ">", "&gt;", CSTR_GT ) \
   f( "", "", CSTR_MAX )

#define CCHAT_CONST_STRS_TABLE_STR( str, esc, id ) bsStatic( str ),

struct tagbstring gc_const_strs[] = {
   CCHAT_CONST_STRS_TABLE( CCHAT_CONST_STRS_TABLE_STR )
};

#define CCHAT_CONST_STRS_TABLE_ESC( str, esc, id ) bsStatic( esc ),

struct tagbstring gc_const_str_escs[] = {
   CCHAT_CONST_STRS_TABLE( CCHAT_CONST_STRS_TABLE_ESC )
};

int cchat_query_key( struct bstrList* array, const char* key, bstring* val_p ) {
   size_t i = 0;
   int retval = 0;
   struct bstrList* key_val_arr = NULL;

   /* Start with the assumption of key not found. */
   if( NULL != *val_p ) {
      bdestroy( *val_p );
      *val_p = NULL;
   }

   if( NULL == array ) {
      /* No list, no value! */
      goto cleanup;
   }

   for( i = 0 ; array->qty > i ; i++ ) {
      key_val_arr = bsplit( array->entry[i], '=' );
      if( NULL == key_val_arr ) {
         /* Couldn't split this pair... */
         continue;
      }

      if( 1 != biseqcstr( key_val_arr->entry[0], key ) ) {
         /* This is the wrong key... */
         bstrListDestroy( key_val_arr );
         key_val_arr = NULL;
         continue;
      }

      /* We've found our key! */
      *val_p = bstrcpy( key_val_arr->entry[1] );
      if( NULL == *val_p ) {
         retval = RETVAL_ALLOC;
      }
      bstrListDestroy( key_val_arr );
      key_val_arr = NULL;
      break;
   }

cleanup:

   if( NULL != key_val_arr ) {
      bstrListDestroy( key_val_arr );
   }

   return retval;
}

int cchat_route_send(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;
   bstring msg_text = NULL;
   bstring err_msg = NULL;

   if( NULL != p ) {
      retval = cchat_query_key( p, "chat", &msg_text );
      if( retval ) {
         goto cleanup;
      }

      retval = chatdb_send_message( db, msg_text, &err_msg );
      if( retval ) {
         goto cleanup;
      }
   }

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   if( NULL != err_msg ) {
      FCGX_FPrintF( req->out, "Location: /?error=%s\r\n", bdata( err_msg ) );
   } else {
      FCGX_FPrintF( req->out, "Location: /\r\n" );
   }
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" );

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;
}

int cchat_print_msg_cb(
   FCGX_Request* req,
   int msg_id, int msg_type, int from, int to, bstring text, time_t msg_time
) {
   int retval = 0;
   size_t i = 0;

   /* Sanitize HTML. */
   while( 0 < blength( &(gc_const_strs[i]) ) ) {
      bfindreplacecaseless(
         text, &(gc_const_strs[i]), &(gc_const_str_escs[i]), 0 );
      i++;
   }

   FCGX_FPrintF( req->out, "<tr><td>%s</td><td>%d</td></tr>\n",
      bdata( text ), msg_time );

   return retval;
}

int cchat_route_root(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;
   size_t i = 0;

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "<html><head><title>Chat</title></head>\n" );
   FCGX_FPrintF( req->out, "<body>\n" );

   /* Show error message if any. */
   if( NULL != q ) {
      for( i = 0 ; q->qty > i ; i++ ) {
         FCGX_FPrintF(
            req->out, "<div class=\"cchat-msg\">%s</div>\n", bdata( q->entry[i] ) );
      }
   }

   /* Show messages. */
   FCGX_FPrintF( req->out, "<table class=\"cchat-messages\">\n" );
   retval = chatdb_iter_messages( req, db, 0, 0, cchat_print_msg_cb );
   FCGX_FPrintF( req->out, "</table>\n" );
   if( retval ) {
      goto cleanup;
   }

   /* Show chat input form. */
   FCGX_FPrintF( req->out, "<form action=\"/send\" method=\"post\">\n" );
   FCGX_FPrintF( req->out, "<input type=\"text\" name=\"chat\" />\n" );
   FCGX_FPrintF( req->out,
      "<input type=\"submit\" name=\"submit\" value=\"Send\" />\n" );
   FCGX_FPrintF( req->out, "</form>\n" );

cleanup:

   /* Close page. */
   FCGX_FPrintF( req->out, "</body>\n" );
   FCGX_FPrintF( req->out, "</html>\n" );

   return retval;
}

/* Translate the tables above into lookup tables. */

#define CCHAT_ROUTES_TABLE_PATHS( path, cb, method ) bsStatic( path ),

struct tagbstring gc_cchat_route_paths[] = {
   CCHAT_ROUTES_TABLE( CCHAT_ROUTES_TABLE_PATHS )
};

#define CCHAT_ROUTES_TABLE_METHODS( path, cb, method ) bsStatic( method ),

struct tagbstring gc_cchat_route_methods[] = {
   CCHAT_ROUTES_TABLE( CCHAT_ROUTES_TABLE_METHODS )
};

#define CCHAT_ROUTES_TABLE_CBS( path, cb, method ) cb,

cchat_route_cb_t gc_cchat_route_cbs[] = {
   CCHAT_ROUTES_TABLE( CCHAT_ROUTES_TABLE_CBS )
};

int cchat_handle_req( FCGX_Request* req, sqlite3* db ) {
   int retval = 0;
   size_t i = 0;
   bstring req_method = NULL;
   bstring req_uri_raw = NULL;
   bstring req_query = NULL;
   bstring err_msg = NULL;
   bstring post_buf = NULL;
   size_t post_buf_sz = 0;
   struct bstrList* req_query_list = NULL;
   struct bstrList* post_buf_list = NULL;

   /* Figure out our request method and consequent action. */
   req_method = bfromcstr( FCGX_GetParam( "REQUEST_METHOD", req->envp ) );
   if( NULL == req_method ) {
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* TODO: URLdecode paths. */
   req_uri_raw = bfromcstr( FCGX_GetParam( "DOCUMENT_URI", req->envp ) );
   if( NULL == req_uri_raw ) {
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* Get query string and split into list. */
   req_query = bfromcstr( FCGX_GetParam( "QUERY_STRING", req->envp ) );
   if( NULL == req_query ) {
      retval = RETVAL_PARAMS;
      goto cleanup;
   }
   
   req_query_list = bsplit( req_query, '&' );

   /* Get POST data (if any). */
   if( 1 == biseqcaselessStatic( req_method, "POST" ) ) {
      post_buf_sz = atoi( FCGX_GetParam( "CONTENT_LENGTH", req->envp ) );
      post_buf = bfromcstralloc( post_buf_sz + 1, "" );
      if( NULL == post_buf || NULL == bdata( post_buf ) ) {
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
      FCGX_GetStr( bdata( post_buf ), post_buf_sz, req->in );
      post_buf->slen = strlen( (char*)post_buf->data );
      if( post_buf->slen >= post_buf->mlen ) {
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
      post_buf_list = bsplit( post_buf, '&' );
      if( NULL == post_buf_list ) {
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   /* Determine if a valid route was provided using LUTs above. */
   while( 0 != blength( &(gc_cchat_route_paths[i]) ) ) {
      if( 0 == bstrcmp( &(gc_cchat_route_paths[i]), req_uri_raw ) ) {
         break;
      }
      i++;
   }

   if(
      0 != blength( &(gc_cchat_route_paths[i]) ) &&
      0 == bstrcmp( &(gc_cchat_route_methods[i]), req_method )
   ) {
      /* A valid route was found! */
      retval = gc_cchat_route_cbs[i]( req, req_query_list, post_buf_list, db );
   } else {
      FCGX_FPrintF( req->out, "Status: 404 Bad Request\r\n\r\n" );
   }

cleanup:

   if( NULL != post_buf_list ) {
      bstrListDestroy( post_buf_list );
   }

   if( NULL != post_buf ) {
      bdestroy( post_buf );
   }

   if( NULL != req_query_list ) {
      bstrListDestroy( req_query_list );
   }

   if( NULL != req_query ) {
      bdestroy( req_query );
   }

   if( NULL != req_method ) {
      bdestroy( req_method );
   }

   if( NULL != req_uri_raw ) {
      bdestroy( req_uri_raw );
   }
 
   return retval;
}


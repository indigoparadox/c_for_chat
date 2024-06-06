
#include "main.h"

#include <stdlib.h> /* for atoi() */

typedef int (*cchat_route_cb_t)(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db );

#define CCHAT_ROUTES_TABLE( f ) \
   f( "/login", cchat_route_login, "GET" ) \
   f( "/auth", cchat_route_auth, "POST" ) \
   f( "/send", cchat_route_send, "POST" ) \
   f( "/chat", cchat_route_chat, "GET" ) \
   f( "", NULL, "" )

static int cchat_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   bstring page_title, bstring page_text
) {
   int retval = 0;
   size_t i = 0;
   bstring err_msg = NULL;
   bstring err_msg_decoded = NULL;
   bstring err_msg_escaped = NULL;

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF(
      req->out, "<html><head><title>%s</title></head>\n", bdata( page_title ) );
   FCGX_FPrintF( req->out, "<body>\n" );

   /* Show error message if any. */
   if( NULL != q ) {
      for( i = 0 ; q->qty > i ; i++ ) {
         retval = bcgi_query_key( q, "error", &err_msg );
         if( retval ) {
            dbglog_error( "error processing query string!\n" );
            goto cleanup;
         }
      }

      if( NULL != err_msg ) {
         /* Decode HTML from query string. */
         retval = bcgi_urldecode( err_msg, &err_msg_decoded );
         if( retval ) {
            goto cleanup;
         }

         /* Sanitize HTML. */
         retval = bcgi_html_escape( err_msg_decoded, &err_msg_escaped );
         if( retval ) {
            goto cleanup;
         }

         FCGX_FPrintF(
            req->out, "<div class=\"cchat-msg\">%s</div>\n",
            bdata( err_msg_escaped ) );
      }
   }

   FCGX_FPrintF( req->out, bdata( page_text ) );

cleanup:

   if( NULL == err_msg_decoded ) {
      bdestroy( err_msg_decoded );
   }

   if( NULL == err_msg_escaped ) {
      bdestroy( err_msg_escaped );
   }

   if( NULL == err_msg ) {
      bdestroy( err_msg );
   }

   /* Close page. */
   FCGX_FPrintF( req->out, "</body>\n" );
   FCGX_FPrintF( req->out, "</html>\n" );

   return retval;

}

int cchat_route_login(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;

   return retval;
}

int cchat_route_auth(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;

   return retval;
}

int cchat_route_send(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;
   bstring msg_text = NULL;
   bstring msg_text_decode = NULL;
   bstring err_msg = NULL;

   dbglog_debug( 1, "route: send\n" );

   if( NULL != p ) {
      retval = bcgi_query_key( p, "chat", &msg_text );
      if( retval ) {
         goto cleanup;
      }

      if( NULL == msg_text ) {
         dbglog_error( "no message text found!\n" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }

      dbglog_debug( 1, "msg_text: %s\n", bdata( msg_text ) );

      retval = bcgi_urldecode( msg_text, &msg_text_decode );
      if( retval ) {
         goto cleanup;
      }

      retval = chatdb_send_message( db, msg_text_decode, &err_msg );
      if( retval ) {
         goto cleanup;
      }
   }

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   if( NULL != err_msg ) {
      FCGX_FPrintF(
         req->out, "Location: /chat?error=%s\r\n", bdata( err_msg ) );
   } else {
      FCGX_FPrintF( req->out, "Location: /chat\r\n" );
   }
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" );

cleanup:

   if( NULL != msg_text_decode ) {
      bdestroy( msg_text_decode );
   }

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;
}

int cchat_print_msg_cb(
   bstring page_text,
   int msg_id, int msg_type, int from, int to, bstring text, time_t msg_time
) {
   int retval = 0;
   bstring text_escaped = NULL;
   bstring msg_line = NULL;

   /* Sanitize HTML. */
   retval = bcgi_html_escape( text, &text_escaped );
   if( retval ) {
      goto cleanup;
   }

   msg_line = bformat( "<tr><td>%s</td><td>%d</td></tr>\n",
      bdata( text_escaped ), msg_time );
   if( NULL == msg_line ) {
      dbglog_error( "could not allocate msg line!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = bconcat( page_text, msg_line );
   if( NULL == msg_line ) {
      dbglog_error( "could not add message to page!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

cleanup:

   if( NULL != msg_line ) {
      bdestroy( msg_line );
   }

   if( NULL != text_escaped ) {
      bdestroy( text_escaped );
   }

   return retval;
}

int cchat_route_chat(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p, sqlite3* db
) {
   int retval = 0;
   bstring page_text = NULL;
   struct tagbstring page_title = bsStatic( "Chat" );

   page_text = bfromcstr( "" );

   /* Show messages. */
   retval = bcatcstr( page_text, "<table class=\"cchat-messages\">\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error starting table!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
   retval = chatdb_iter_messages( page_text, db, 0, 0, cchat_print_msg_cb );
   if( retval ) {
      goto cleanup;
   }
   retval = bcatcstr( page_text, "</table>\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error ending table!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* Show chat input form. */
   retval = bcatcstr(
      page_text,
      "<form action=\"/send\" method=\"post\">\n"
         "<input type=\"text\" name=\"chat\" />\n"
         "<input type=\"submit\" name=\"submit\" value=\"Send\" />\n"
      "</form>\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error adding form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = cchat_page( req, q, p, &page_title, page_text );

cleanup:

   if( NULL != page_text ) {
      bdestroy( page_text );
   }

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
   bstring post_buf = NULL;
   size_t post_buf_sz = 0;
   struct bstrList* req_query_list = NULL;
   struct bstrList* post_buf_list = NULL;

   /* Figure out our request method and consequent action. */
   req_method = bfromcstr( FCGX_GetParam( "REQUEST_METHOD", req->envp ) );
   if( NULL == req_method ) {
      dbglog_error( "invalid request params!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* TODO: URLdecode paths. */
   req_uri_raw = bfromcstr( FCGX_GetParam( "DOCUMENT_URI", req->envp ) );
   if( NULL == req_uri_raw ) {
      dbglog_error( "invalid request URI!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* Get query string and split into list. */
   req_query = bfromcstr( FCGX_GetParam( "QUERY_STRING", req->envp ) );
   if( NULL == req_query ) {
      dbglog_error( "invalid request query string!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }
   
   req_query_list = bsplit( req_query, '&' );

   /* Get POST data (if any). */
   if( 1 == biseqcaselessStatic( req_method, "POST" ) ) {
      /* Allocate buffer to hold POST data. */
      post_buf_sz = atoi( FCGX_GetParam( "CONTENT_LENGTH", req->envp ) );
      post_buf = bfromcstralloc( post_buf_sz + 1, "" );
      if( NULL == post_buf || NULL == bdata( post_buf ) ) {
         dbglog_error( "could not allocate POST buffer!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
      FCGX_GetStr( bdata( post_buf ), post_buf_sz, req->in );
      post_buf->slen = post_buf_sz;
      post_buf->data[post_buf_sz] = '\0';

      /* Validate post_buf size. */
      if( post_buf->slen >= post_buf->mlen ) {
         dbglog_error( "invalid POST buffer size! (s: %d, m: %d)\n",
            post_buf->slen, post_buf->mlen );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
 
      /* Split post_buf into a key/value array. */
      post_buf_list = bsplit( post_buf, '&' );
      if( NULL == post_buf_list ) {
         dbglog_error( "could not split POST buffer!\n" );
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


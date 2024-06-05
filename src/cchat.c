
#include "chatdb.h"
#include "cchat.h"

typedef int (*cchat_route_cb_t)( FCGX_Request* req, sqlite3* db );

#define CCHAT_ROUTES_TABLE( f ) \
   f( "/send", cchat_route_send, "POST" ) \
   f( "/", cchat_route_root, "GET" ) \
   f( "", NULL, "" )

int cchat_route_send( FCGX_Request* req, sqlite3* db ) {
   int retval = 0;
   bstring msg_text = NULL;

   /* TODO: Actual message text. */
   msg_text = bfromcstr( "test" );
   if( NULL == msg_text ) {
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = chatdb_send_message( db, msg_text );

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   FCGX_FPrintF( req->out, "Location: /\r\n" );
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" );

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   return retval;
}

int cchat_print_msg_cb(
   int msg_id, int msg_type, int from, int to, bstring text
) {
   int retval = 0;

   /* TODO: Sanitize HTML. */

   return retval;
}

int cchat_route_root( FCGX_Request* req, sqlite3* db ) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "<html><head><title>Chat</title></head>\n" );
   FCGX_FPrintF( req->out, "<body>\n" );
   FCGX_FPrintF( req->out, "<form action=\"/send\" method=\"post\">\n" );
   FCGX_FPrintF( req->out, "<input type=\"text\" name=\"chat\" />\n" );
   FCGX_FPrintF( req->out,
      "<input type=\"submit\" name=\"submit\" value=\"Send\" />\n" );
   FCGX_FPrintF( req->out, "</form>\n" );
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
      retval = gc_cchat_route_cbs[i]( req, db );
   } else {
      FCGX_FPrintF( req->out, "Status: 404 Bad Request\r\n\r\n" );
   }

cleanup:

   if( NULL != req_method ) {
      bdestroy( req_method );
   }

   if( NULL != req_uri_raw ) {
      bdestroy( req_uri_raw );
   }
 
   return retval;
}


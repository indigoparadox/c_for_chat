
#include "main.h"

#include <stdint.h>
#include <stdlib.h> /* for atoi() */

typedef int (*cchat_route_cb_t)(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db );

#define CCHAT_PAGE_FLAG_NONAV    0x01

#define CCHAT_ROUTES_TABLE( f ) \
   f( "/logout", cchat_route_logout, "GET" ) \
   f( "/profile", cchat_route_profile, "GET" ) \
   f( "/user", cchat_route_user, "POST" ) \
   f( "/login", cchat_route_login, "GET" ) \
   f( "/auth", cchat_route_auth, "POST" ) \
   f( "/send", cchat_route_send, "POST" ) \
   f( "/chat", cchat_route_chat, "GET" ) \
   f( "/style.css", cchat_route_style, "GET" ) \
   f( "/", cchat_route_root, "GET" ) \
   f( "", NULL, "" )

#define cchat_decode_field( list, field_name ) \
   retval = bcgi_query_key( list, #field_name, &field_name ); \
   if( retval || NULL == field_name ) { \
      err_msg = bfromcstr( "Invalid " #field_name "!" ); \
      dbglog_error( "no " #field_name " found!\n" ); \
      retval = RETVAL_PARAMS; \
      goto cleanup; \
   } \
   retval = bcgi_urldecode( field_name, &field_name ## _decode ); \
   if( retval ) { \
      goto cleanup; \
   }

static int cchat_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   bstring page_title, bstring page_text, uint8_t flags
) {
   int retval = 0;
   size_t i = 0;
   bstring err_msg = NULL;
   bstring err_msg_decoded = NULL;
   bstring err_msg_escaped = NULL;

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "<html>\n" );
   FCGX_FPrintF( req->out, "<head><title>%s</title>\n", bdata( page_title ) );
   FCGX_FPrintF( req->out, "<link rel=\"stylesheet\" href=\"style.css\" />\n" );
   FCGX_FPrintF( req->out, "</head>\n" );
   FCGX_FPrintF( req->out, "<body>\n" );
   FCGX_FPrintF( req->out, "<h1 class=\"page-title\">%s</h1>\n",
      bdata( page_title ) );
   if( CCHAT_PAGE_FLAG_NONAV != (CCHAT_PAGE_FLAG_NONAV & flags) ) {
      FCGX_FPrintF( req->out, "<ul class=\"page-nav\">\n" );
      FCGX_FPrintF( req->out, "<li><a href=\"/chat\">Chat</a>\n" );
      FCGX_FPrintF( req->out, "<li><a href=\"/profile\">Profile</a>\n" );
      FCGX_FPrintF( req->out, "<li><a href=\"/logout\">Logout</a>\n" );
      FCGX_FPrintF( req->out, "</ul>\n" );
   }

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
            req->out, "<div class=\"page-error\">%s</div>\n",
            bdata( err_msg_escaped ) );
      }
   }

   FCGX_FPrintF( req->out, "%s", bdata( page_text ) );

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

int cchat_route_logout(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   bstring session = NULL;

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( !retval && NULL != session ) {
      retval = chatdb_remove_session( NULL, db, session, NULL );
   }

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   FCGX_FPrintF( req->out, "Location: /login\r\n" );
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   if( NULL != session ) {
      bdestroy( session );
   }

   return retval;
}

int cchat_profile_form(
   bstring page_text, bstring user_name, bstring email, bstring session
) {
   int retval = 0;

   retval = bassignformat(
      page_text,
      "<div class=\"profile-form\">\n"
      "<form action=\"/user\" method=\"post\">\n"
         "<div class=\"profile-field\">"
            "<label for=\"user\">Username: </label>"
            "<input type=\"text\" id=\"user\" name=\"user\" value=\"%s\" />"
               "</div>\n"
         "<div class=\"profile-field\">"
            "<label for=\"user\">E-Mail: </label>"
            "<input type=\"text\" id=\"email\" name=\"email\" value=\"%s\" />"
               "</div>\n"
         "<div class=\"profile-field\">"
            "<label for=\"password1\">New password: </label>"
            "<input type=\"password\" id=\"password1\" name=\"password1\" />"
               "</div>\n"
         "<div class=\"profile-field\">"
            "<label for=\"password2\">Confirm password: </label>"
            "<input type=\"password\" id=\"password2\" name=\"password2\" />"
               "</div>\n"
         "<div class=\"profile-field profile-button\">"
            "<input type=\"submit\" name=\"submit\" value=\"Submit\" />"
         "</div>\n",
      bdata( user_name ), bdata( email )
   );

   if( BSTR_ERR == retval ) {
      dbglog_error( "unable to allocate profile form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   if( NULL != session ) {
      retval = bformata( page_text,
         "<input type=\"hidden\" name=\"csrf\" value=\"%s\" />\n",
         bdata( session ) );
      if( BSTR_ERR == retval ) {
         dbglog_error( "unable to allocate profile form!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   retval = bcatcstr( page_text, "</form>\n</div>\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "unable to allocate profile form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
 
cleanup:

   return retval;
}

int cchat_profile_user_cb(
   bstring page_text, FCGX_Request* req,
   bstring password_test, int* user_id_out_p,
   int user_id, bstring user_name, bstring email,
   bstring hash, size_t hash_sz, bstring salt, size_t iters, time_t msg_time
) {
   int retval = 0;
   bstring session = NULL;
   bstring req_cookie = NULL;
   struct bstrList* c = NULL;

   req_cookie = bfromcstr( FCGX_GetParam( "HTTP_COOKIE", req->envp ) );
   if( NULL == req_cookie ) {
      dbglog_error( "no cookie provided to user validator!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   c = bsplit( req_cookie, '&' );
   if( NULL == c ) {
      dbglog_error( "could not allocate cookie list!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( retval ) {
      goto cleanup;
   }

   retval = cchat_profile_form( page_text, user_name, email, session );

cleanup:

   if( NULL != c ) {
      bstrListDestroy( c );
   }

   if( NULL != req_cookie ) {
      bdestroy( req_cookie );
   }

   if( NULL != session ) {
      bdestroy( session );
   }

   return retval;
}

int cchat_route_profile(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   struct tagbstring page_title = bsStatic( "Profile" );
   bstring page_text = NULL;
   struct tagbstring empty_string = bsStatic( "" );

   page_text = bfromcstr( "" );
   if( NULL == page_text ) {
      retval = RETVAL_ALLOC;
      dbglog_error( "could not allocate profile form!\n" );
      goto cleanup;
   }

   if( 0 <= auth_user_id ) {
      /* Edit an existing user. */
      retval = chatdb_iter_users(
         page_text, db, req, NULL, auth_user_id,
         NULL, NULL, cchat_profile_user_cb, NULL );
      if( retval ) {
         goto cleanup;
      }
   } else {
      retval = cchat_profile_form(
         page_text, &empty_string, &empty_string, NULL );
   }

   retval = cchat_page( req, q, p, &page_title, page_text, 0 );

cleanup:

   if( NULL != page_text ) {
      bdestroy( page_text );
   }

   return retval;
}

int cchat_route_user(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   bstring user = NULL;
   bstring user_decode = NULL;
   bstring password1 = NULL;
   bstring password1_decode = NULL;
   bstring password2 = NULL;
   bstring password2_decode = NULL;
   bstring email = NULL;
   bstring email_decode = NULL;
   bstring err_msg = NULL;
   bstring session = NULL;
   bstring csrf = NULL;
   bstring csrf_decode = NULL;

   dbglog_debug( 1, "route: user\n" );

   if( NULL == p ) {
      err_msg = bfromcstr( "Invalid message format!" );
      goto cleanup;
   }

   if( 0 <= auth_user_id ) {
      /* See if a valid session exists (don't urldecode!). */
      retval = bcgi_query_key( c, "session", &session );
      if( retval || NULL == session ) {
         dbglog_error( "unable to determine session cookie hash!\n" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }

      cchat_decode_field( p, csrf );

      /* Validate CSRF token. */
      if( 0 != bstrcmp( csrf_decode, session ) ) {
         dbglog_error( "invalid csrf!\n" );
         assert( NULL == err_msg );
         err_msg = bfromcstr( "Invalid CSRF token!" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field( p, user );
   cchat_decode_field( p, password1 );
   cchat_decode_field( p, password2 );
   cchat_decode_field( p, email );

   if( 0 != bstrcmp( password1, password2 ) ) {
      dbglog_error( "password fields do no match!\n" );
      err_msg = bfromcstr( "Password fields do not match!" );
      goto cleanup;
   }

   dbglog_debug( 1, "adding user: %s\n", bdata( user ) );

   retval = chatdb_add_user(
      db, auth_user_id, user_decode, password1_decode, email_decode, &err_msg );

cleanup:

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   if( NULL != err_msg ) {
      FCGX_FPrintF(
         req->out, "Location: /profile?error=%s\r\n", bdata( err_msg ) );
   } else if( 0 <= auth_user_id ) {
      FCGX_FPrintF( req->out, "Location: /profile\r\n" );
   } else {
      FCGX_FPrintF( req->out, "Location: /login\r\n" );
   }
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   if( NULL != csrf ) {
      bdestroy( csrf );
   }

   if( NULL != csrf_decode ) {
      bdestroy( csrf_decode );
   }

   if( NULL != session ) {
      bdestroy( session );
   }

   if( NULL != email_decode ) {
      bdestroy( email_decode );
   }

   if( NULL != email ) {
      bdestroy( email );
   }

   if( NULL != user_decode ) {
      bdestroy( user_decode );
   }

   if( NULL != user ) {
      bdestroy( user );
   }

   if( NULL != password1_decode ) {
      bdestroy( password1_decode );
   }

   if( NULL != password1 ) {
      bdestroy( password1 );
   }

   if( NULL != password2_decode ) {
      bdestroy( password2_decode );
   }

   if( NULL != password2 ) {
      bdestroy( password2 );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;

}

int cchat_route_login(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   struct tagbstring page_title = bsStatic( "Login" );
   struct tagbstring page_text = bsStatic(
      "<div class=\"login-form\">\n"
      "<form action=\"/auth\" method=\"post\">\n"
         "<div class=\"login-field\">"
            "<label for=\"user\">Username: </label>"
            "<input type=\"text\" id=\"user\" name=\"user\" /></div>\n"
         "<div class=\"login-field\">"
            "<label for=\"password\">Password: </label>"
            "<input type=\"password\" id=\"password\" name=\"password\" />"
               "</div>\n"
         "<div class=\"login-field login-button\">"
            "<input type=\"submit\" name=\"submit\" value=\"Login\" /></div>\n"
      "</form>\n"
      "</div>\n" );

   retval = cchat_page(
      req, q, p, &page_title, &page_text, CCHAT_PAGE_FLAG_NONAV );

   return retval;
}

int cchat_auth_user_cb(
   bstring page_text, FCGX_Request* req,
   bstring password_test, int* user_id_out_p,
   int user_id, bstring user_name, bstring email,
   bstring hash, size_t hash_sz, bstring salt, size_t iters, time_t msg_time
) {
   int retval = 0;
   bstring hash_test = NULL;

   assert( NULL != password_test );
   dbglog_error( "test: %s\n", bdata( password_test ) );

   retval = chatdb_hash_password(
      password_test, iters, hash_sz, salt, &hash_test );
   if( retval ) {
      goto cleanup;
   }

   /* Test the provided password. */
   if( 0 != bstrcmp( hash, hash_test ) ) {
      retval = RETVAL_AUTH;
      goto cleanup;
   }

   assert( 0 != user_id );

   /* Return this user as their password matches. */
   *user_id_out_p = user_id;

cleanup:

   if( NULL != hash_test ) {
      bdestroy( hash_test );
   }

   return retval;
}

int cchat_route_auth(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   bstring user = NULL;
   bstring user_decode = NULL;
   bstring password = NULL;
   bstring password_decode = NULL;
   bstring err_msg = NULL;
   bstring hash = NULL;
   bstring remote_host = NULL;
   int user_id = -1;

   dbglog_debug( 1, "route: auth\n" );

   if( NULL == p ) {
      err_msg = bfromcstr( "Invalid message format!" );
      goto cleanup;
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field( p, user );
   cchat_decode_field( p, password );

   /* Validate username and password. */
   retval = chatdb_iter_users(
      NULL, db, req, user_decode, -1, password_decode, &user_id,
      cchat_auth_user_cb, &err_msg );
   if( retval || 0 > user_id ) {
      if( NULL != err_msg ) {
         retval = bassigncstr( err_msg, "Invalid username or password!" );
         if( BSTR_OK != retval ) {
            dbglog_error( "unable to allocate error message!\n" );
            retval = RETVAL_ALLOC;
         }
      } else {
         err_msg = bfromcstr( "Invalid username or password!" );
         if( NULL == err_msg ) {
            dbglog_error( "unable to allocate error message!\n" );
            retval = RETVAL_ALLOC;
         }
      }
      goto cleanup;
   }

   remote_host = bfromcstr( FCGX_GetParam( "REMOTE_ADDR", req->envp ) );

   retval = chatdb_add_session( db, user_id, remote_host, &hash, &err_msg );
   if( retval ) {
      assert( NULL != err_msg );
      goto cleanup;
   }

   /* Set auth cookie. */
   FCGX_FPrintF( req->out, "Set-Cookie: session=%s; Max-Age=3600; HttpOnly\r\n",
      bdata( hash ) );

cleanup:

   /* Redirect to route. */
   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   if( NULL != err_msg ) {
      FCGX_FPrintF(
         req->out, "Location: /login?error=%s\r\n", bdata( err_msg ) );
   } else {
      FCGX_FPrintF( req->out, "Location: /chat\r\n" );
   }
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" );

   if( NULL != remote_host ) {
      bdestroy( remote_host );
   }

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != user_decode ) {
      bdestroy( user_decode );
   }

   if( NULL != user ) {
      bdestroy( user );
   }

   if( NULL != password_decode ) {
      bdestroy( password_decode );
   }

   if( NULL != password ) {
      bdestroy( password );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;

}

int cchat_route_send(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   bstring chat = NULL;
   bstring chat_decode = NULL;
   bstring err_msg = NULL;
   bstring session = NULL;
   bstring csrf = NULL;
   bstring csrf_decode = NULL;

   dbglog_debug( 1, "route: send\n" );

   if( NULL == p ) {
      err_msg = bfromcstr( "Invalid message format!" );
      goto cleanup;
   }

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( retval || NULL == session ) {
      dbglog_error( "unable to determine session cookie hash!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   cchat_decode_field( p, csrf );

   /* Validate CSRF token. */
   if( 0 != bstrcmp( csrf_decode, session ) ) {
      dbglog_error( "invalid csrf!\n" );
      assert( NULL == err_msg );
      err_msg = bfromcstr( "Invalid CSRF token!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field( p, chat );

   retval = chatdb_send_message( db, auth_user_id, chat_decode, &err_msg );
   if( retval ) {
      goto cleanup;
   }

cleanup:

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

   if( NULL != csrf ) {
      bdestroy( csrf );
   }

   if( NULL != csrf_decode ) {
      bdestroy( csrf_decode );
   }

   if( NULL != session ) {
      bdestroy( session );
   }

   if( NULL != chat_decode ) {
      bdestroy( chat_decode );
   }

   if( NULL != chat ) {
      bdestroy( chat );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;
}

int cchat_print_msg_cb(
   bstring page_text,
   int msg_id, int msg_type, bstring from, int to, bstring text, time_t msg_time
) {
   int retval = 0;
   bstring text_escaped = NULL;
   bstring msg_line = NULL;

   /* Sanitize HTML. */
   retval = bcgi_html_escape( text, &text_escaped );
   if( retval ) {
      goto cleanup;
   }

   msg_line = bformat(
      "<tr>"
         "<td class=\"chat-from\">%s</td>"
         "<td class=\"chat-msg\">%s</td>"
         "<td class=\"chat-time\">%d</td>"
      "</tr>\n",
      bdata( from ), bdata( text_escaped ), msg_time );
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
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;
   bstring page_text = NULL;
   struct tagbstring page_title = bsStatic( "Chat" );
   bstring err_msg = NULL;
   bstring session = NULL;

   if( 0 > auth_user_id ) {
      /* Invalid user; redirect to login. */
      FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
      FCGX_FPrintF( req->out, "Location: /login\r\n" );
      FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
      FCGX_FPrintF( req->out, "\r\n" );
      goto cleanup;
   }

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( retval || NULL == session ) {
      dbglog_error( "unable to determine session cookie hash!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   page_text = bfromcstr( "" );

   /* Show messages. */
   retval = bcatcstr( page_text, "<table class=\"chat-messages\">\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error starting table!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
   retval = chatdb_iter_messages(
      page_text, db, 0, 0, cchat_print_msg_cb, &err_msg );
   if( retval ) {
      dbglog_error( "error iteraing messages!\n" );
      goto cleanup;
   }
   retval = bcatcstr( page_text, "</table>\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error ending table!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   if( NULL != err_msg ) {
      retval = bconcat( page_text, err_msg );
      if( BSTR_ERR == retval ) {
         dbglog_error( "error displaying message: %s\n", bdata( err_msg ) );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   /* Show chat input form. */
   retval = bformata(
      page_text,
      "<div class=\"chat-form\">\n"
      "<form action=\"/send\" method=\"post\">\n"
         "<input type=\"text\" id=\"chat\" name=\"chat\" />\n"
         "<input type=\"submit\" name=\"submit\" value=\"Send\" />\n"
         "<input type=\"hidden\" name=\"csrf\" value=\"%s\" />\n"
      "</form>\n"
      "</div>\n",
      bdata( session ) );
   if( BSTR_ERR == retval ) {
      dbglog_error( "error adding form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = cchat_page( req, q, p, &page_title, page_text, 0 );

cleanup:

   if( NULL != session ) {
      bdestroy( session );
   }

   if( NULL != page_text ) {
      bdestroy( page_text );
   }

   if( NULL != err_msg ) {
      bdestroy( err_msg );
   }

   return retval;
}

int cchat_route_style(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   FILE* fp = NULL;
   int retval = 0;
   bstring contents = NULL;

   fp = fopen( "style.css", "rb");
   if( NULL == fp ) {
      dbglog_error( "couldn't open style.css!\n" );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   contents = bread( (bNread)fread, fp );
   if( NULL == contents ) {
      dbglog_error( "couldn't read style.css!\n" );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "%s", bdata( contents ) );

cleanup:

   if( NULL != contents ) {
      bdestroy( contents );
   }

   if( NULL != fp ) {
      fclose( fp );
   }

   return retval;
}

int cchat_route_root(
   FCGX_Request* req, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c, sqlite3* db
) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Status: 303 See Other\r\n" );
   if( 0 > auth_user_id ) {
      /* Invalid user; redirect to login. */
      FCGX_FPrintF( req->out, "Location: /login\r\n" );
   } else {
      FCGX_FPrintF( req->out, "Location: /chat\r\n" );
   }
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" );

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

int cchat_auth_session_cb(
   bstring page_text, int* user_id_out_p,
   int session_id, int user_id,
   bstring hash, size_t hash_sz, bstring remote_host, time_t start_time
) {
   int retval = 0;

   /* TODO: Check remote host. */
   *user_id_out_p = user_id;

   return retval;
}

int cchat_handle_req( FCGX_Request* req, sqlite3* db ) {
   int retval = 0;
   size_t i = 0;
   bstring req_method = NULL;
   bstring req_uri_raw = NULL;
   bstring req_query = NULL;
   bstring post_buf = NULL;
   bstring req_cookie = NULL;
   bstring session = NULL;
   struct tagbstring remote_host = bsStatic( "127.0.0.1" ); /* TODO */
   size_t post_buf_sz = 0;
   struct bstrList* req_query_list = NULL;
   struct bstrList* post_buf_list = NULL;
   struct bstrList* req_cookie_list = NULL;
   int auth_user_id = -1;

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

   /* Get cookies and split into list. */
   req_cookie = bfromcstr( FCGX_GetParam( "HTTP_COOKIE", req->envp ) );
   if( NULL != req_cookie ) {
      req_cookie_list = bsplit( req_cookie, '&' );
      if( NULL == req_cookie_list ) {
         dbglog_error( "could not allocate cookie list!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }

      /* See if a valid session exists (don't urldecode!). */
      retval = bcgi_query_key( req_cookie_list, "session", &session );
      if( !retval && NULL != session ) {
         chatdb_iter_sessions(
            NULL, &auth_user_id, db, session,
            &remote_host, cchat_auth_session_cb, NULL );
      }
   }

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
      retval = gc_cchat_route_cbs[i](
         req, auth_user_id,
         req_query_list, post_buf_list, req_cookie_list, db );
   } else {
      FCGX_FPrintF( req->out, "Status: 404 Bad Request\r\n\r\n" );
   }

cleanup:

   if( NULL != session ) {
      bdestroy( session );
   }

   if( NULL != req_cookie_list ) {
      bstrListDestroy( req_cookie_list );
   }

   if( NULL != req_cookie ) {
      bdestroy( req_cookie );
   }

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


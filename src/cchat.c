
#include "main.h"

#include <stdlib.h> /* for atoi() */

typedef int (*cchat_route_cb_t)(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c );

extern bstring g_recaptcha_site_key;
extern bstring g_recaptcha_secret_key;

#define CCHAT_ROUTES_TABLE( f ) \
   f( "/logout", cchat_route_logout, "GET" ) \
   f( "/profile", cchat_route_profile, "GET" ) \
   f( "/user", cchat_route_user, "POST" ) \
   f( "/login", cchat_route_login, "GET" ) \
   f( "/auth", cchat_route_auth, "POST" ) \
   f( "/send", cchat_route_send, "POST" ) \
   f( "/chat", cchat_route_chat, "GET" ) \
   f( "/style.css", cchat_route_style_css, "GET" ) \
   f( "/chat.js", cchat_route_chat_js, "GET" ) \
   f( "/alert.mp3", cchat_route_alert_mp3, "GET" ) \
   f( "/", cchat_route_root, "GET" ) \
   f( "", NULL, "" )

#define cchat_decode_field_rename( def, list, f_name, decode_name, post_name ) \
   retval = bcgi_query_key( list, #post_name, &f_name ); \
   if( retval || (NULL == def && NULL == f_name) ) { \
      err_msg = bfromcstr( "Invalid " #post_name "!" ); \
      dbglog_error( "no " #post_name " found!\n" ); \
      retval = RETVAL_PARAMS; \
      goto cleanup; \
   } else if( !retval && NULL != f_name ) { \
      retval = bcgi_urldecode( f_name, &(decode_name) ); \
      if( retval ) { \
         goto cleanup; \
      } \
   } else { \
      assert( NULL == decode_name ); \
      decode_name = bfromcstr( def ); \
   }

#define cchat_decode_field( def, list, field_name ) \
   cchat_decode_field_rename( \
      def, list, field_name, field_name ## _decode, field_name );

int cchat_route_logout(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   bstring session = NULL;
   const struct tagbstring cs_login = bsStatic( "/login" );

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( !retval && NULL != session ) {
      retval = chatdb_remove_session( NULL, op, session, NULL );
   }

   /* Redirect to route. */
   webutil_redirect( &(op->req), &cs_login, 0 );

   if( NULL != session ) {
      bdestroy( session );
   }

   return retval;
}

static int cchat_profile_form(
   struct WEBUTIL_PAGE* page, bstring user_name, bstring email, bstring session,
   int session_timeout, int flags
) {
   int retval = 0;

   retval = bassignformat(
      page->text,
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
         "<div class=\"profile-field\">"
            "<label for=\"session_timeout\">Session timeout: </label>"
            "<input type=\"text\" id=\"session_timeout\" "
               "name=\"session_timeout\" value=\"%d\" />"
               "</div>\n"
         "<div class=\"profile-field\">"
            "<label for=\"flags_ws\">Use websockets: </label>"
            "<input type=\"checkbox\" id=\"flags_ws\" "
               "name=\"flags_ws\"%s /></div>\n",
      bdata( user_name ), bdata( email ), session_timeout,
      (CHATDB_USER_FLAG_WS == (CHATDB_USER_FLAG_WS & flags)) ?
         " checked=\"checked\"" : ""
   );

   if( BSTR_ERR == retval ) {
      dbglog_error( "unable to allocate profile form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   if( NULL != session ) {
      retval = bformata( page->text,
         "<input type=\"hidden\" name=\"csrf\" value=\"%s\" />\n",
         bdata( session ) );
      if( BSTR_ERR == retval ) {
         dbglog_error( "unable to allocate profile form!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   if( NULL != bdata( g_recaptcha_site_key ) ) {
      /* Add recaptcha if key present. */
      retval = bformata( page->text,
         "<div class=\"g-recaptcha\" data-sitekey=\"%s\"></div>\n",
         bdata( g_recaptcha_site_key ) );
      if( BSTR_ERR == retval ) {
         dbglog_error( "unable to allocate profile form!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }

      retval = webutil_add_script( page,
         "<script src=\"https://www.google.com/recaptcha/api.js\" "
            "async defer></script>\n" );
      if( retval ) {
         goto cleanup;
      }
   }

   retval = bcatcstr( page->text,
      "<div class=\"profile-field profile-button\">"
         "<input type=\"submit\" name=\"submit\" value=\"Submit\" />"
      "</div>\n</form>\n</div>\n" );
   if( BSTR_ERR == retval ) {
      dbglog_error( "unable to allocate profile form!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
 
cleanup:

   return retval;
}

int cchat_profile_user_cb(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   bstring password_test, struct CHATDB_USER* user
) {
   int retval = 0;
   bstring session = NULL;
   bstring req_cookie = NULL;
   struct bstrList* c = NULL;

   if( webutil_get_cookies( &c, op ) ) {
      goto cleanup;
   }

   /* See if a valid session exists (don't urldecode!). */
   retval = bcgi_query_key( c, "session", &session );
   if( retval ) {
      goto cleanup;
   }
   dbglog_debug( 1, "profile session: %s\n", bdata( session ) );

   retval = cchat_profile_form(
      page, user->user_name, user->email, session, user->session_timeout,
      user->flags );

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
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   struct tagbstring page_title = bsStatic( "Profile" );
   struct WEBUTIL_PAGE page = { 0, 0, 0 };
   struct tagbstring empty_string = bsStatic( "" );
   struct CHATDB_USER user;

   memset( &user, '\0', sizeof( struct CHATDB_USER ) );

   page.title = &page_title;
   page.flags = 0;
   page.text = bfromcstr( "" );
   if( NULL == page.text ) {
      retval = RETVAL_ALLOC;
      dbglog_error( "could not allocate profile form!\n" );
      goto cleanup;
   }

   if( 0 <= auth_user_id ) {
      /* Edit an existing user. */
      user.user_id = auth_user_id;
      retval = chatdb_iter_users(
         &page, op, NULL, &user, cchat_profile_user_cb, NULL );
      if( retval ) {
         goto cleanup;
      }
   } else {
      retval = cchat_profile_form(
         &page, &empty_string, &empty_string, NULL, 3600, 0 );
   }

   retval = webutil_show_page( &(op->req), q, p, &page );

cleanup:

   bcgi_cleanup_bstr( page.scripts, likely );
   bcgi_cleanup_bstr( page.text, likely );

   chatdb_free_user( &user );

   return retval;
}

int cchat_route_user(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   bstring user = NULL;
   bstring password1 = NULL;
   bstring password1_decode = NULL;
   bstring password2 = NULL;
   bstring password2_decode = NULL;
   bstring email = NULL;
   bstring err_msg = NULL;
   bstring session = NULL;
   bstring csrf = NULL;
   bstring csrf_decode = NULL;
   bstring recaptcha = NULL;
   bstring recaptcha_decode = NULL;
   bstring session_timeout = NULL;
   bstring session_timeout_decode = NULL;
   bstring flags_ws = NULL;
   bstring flags_ws_decode = NULL;
   bstring redirect_url = NULL;
   struct tagbstring user_forbidden_chars = bsStatic( "&?* \n\r" );
   struct CHATDB_USER user_obj;

   memset( &user_obj, '\0', sizeof( struct CHATDB_USER ) );

   dbglog_debug( 1, "route: user\n" );

   if( NULL == p ) {
      assert( NULL == err_msg );
      err_msg = bfromcstr( "Invalid message format!" );
      goto cleanup;
   }

   if( 0 <= auth_user_id ) {
      /* TODO: Better CSRF. */

      /* See if a valid session exists (don't urldecode!). */
      retval = bcgi_query_key( c, "session", &session );
      if( retval || NULL == session ) {
         dbglog_error( "unable to determine session cookie hash!\n" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }

      cchat_decode_field( NULL, p, csrf );

      /* Validate CSRF token. */
      if( 0 != bstrcmp( csrf_decode, session ) ) {
         dbglog_error( "invalid csrf token!\n" );
         assert( NULL == err_msg );
         err_msg = bfromcstr( "Invalid CSRF token!" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }
   }

   /* Grab recaptcha validation string if applicable. */
   bcgi_query_key( p, "g-recaptcha-response", &recaptcha );
   if( NULL != recaptcha ) {
      retval = bcgi_urldecode( recaptcha, &recaptcha_decode );
      if( retval ) {
         goto cleanup;
      }
      assert( NULL != recaptcha_decode );

      retval = webutil_check_recaptcha( &(op->req), recaptcha_decode );
      if( retval ) {
         assert( NULL == err_msg );
         err_msg = bfromcstr( "Invalid ReCAPTCHA response!" );
         bcgi_check_null( err_msg );
         goto cleanup;
      }
   }

   if( 0 <= auth_user_id ) {
      /* We're editing an existing user, so grab that user. */
      user_obj.user_id = auth_user_id;
      retval = chatdb_iter_users( NULL, op, NULL, &user_obj, NULL, NULL );
      if( retval ) {
         dbglog_error( "error fetching user!\n" );
         goto cleanup;
      }
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field_rename( NULL, p, user, user_obj.user_name, user );
   cchat_decode_field( NULL, p, password1 );
   cchat_decode_field( NULL, p, password2 );
   cchat_decode_field_rename( NULL, p, email, user_obj.email, email );
   cchat_decode_field( NULL, p, session_timeout );
   user_obj.session_timeout = atol( (char*)session_timeout_decode->data );

   /* Decode websocket flags checkbox. */
   cchat_decode_field( "off", p, flags_ws );
   if( biseqStatic( flags_ws_decode, "on" ) ) {
      user_obj.flags |= CHATDB_USER_FLAG_WS;
   } else {
      user_obj.flags &= ~CHATDB_USER_FLAG_WS;
   }

   /* Validate passwords. */
   if( 0 != bstrcmp( password1, password2 ) ) {
      dbglog_error( "password fields do not match!\n" );
      err_msg = bfromcstr( "Password fields do not match!" );
      bcgi_check_null( err_msg );
      goto cleanup;
   }

   /* Validate user field. */
   retval = binchr( user_obj.user_name, 0, &user_forbidden_chars );
   if( BSTR_ERR != retval ) {
      retval = RETVAL_PARAMS;
      dbglog_error( "invalid username specified: %s\n",
         bdata( user_obj.user_name ) );
      err_msg = bformat( "Invalid username specified!" );
      bcgi_check_null( err_msg );
      goto cleanup;
   }
   retval = 0; /* Reset retval. */

   dbglog_debug( 1, "adding user: %s\n", bdata( user_obj.user_name ) );

   /* TODO: Redo add user, add ORM and translate flags checkbox. */
   retval = chatdb_add_user( op, &user_obj, password1_decode, &err_msg );

cleanup:

   /* Redirect to route. */
   if( NULL != err_msg ) {
      redirect_url = bformat( "/profile?error=%s", bdata( err_msg ) );
   } else if( 0 <= auth_user_id ) {
      redirect_url = bfromcstr( "/profile" );
   } else {
      redirect_url = bfromcstr( "/login" );
   }
   bcgi_check_null( redirect_url );
   webutil_redirect( &(op->req), redirect_url, 0 );

   chatdb_free_user( &(user_obj) );

   bcgi_cleanup_bstr( redirect_url, likely );
   bcgi_cleanup_bstr( flags_ws, likely );
   bcgi_cleanup_bstr( flags_ws_decode, likely );
   bcgi_cleanup_bstr( session_timeout, likely );
   bcgi_cleanup_bstr( session_timeout_decode, likely );
   bcgi_cleanup_bstr( recaptcha, likely );
   bcgi_cleanup_bstr( recaptcha_decode, likely );
   bcgi_cleanup_bstr( csrf, likely );
   bcgi_cleanup_bstr( csrf_decode, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( email, likely );
   bcgi_cleanup_bstr( user, likely );
   bcgi_cleanup_bstr( password1, likely );
   bcgi_cleanup_bstr( password1_decode, likely );
   bcgi_cleanup_bstr( password2, likely );
   bcgi_cleanup_bstr( password2_decode, likely );
   bcgi_cleanup_bstr( err_msg, unlikely );

   return retval;

}

int cchat_route_login(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   struct WEBUTIL_PAGE page = { 0, 0, 0 };
   struct tagbstring page_title = bsStatic( "Login" );

   page.text = bfromcstr(
      "<div class=\"login-form\">\n"
      "<form action=\"/auth\" method=\"post\">\n"
         "<div class=\"login-field\">"
            "<label for=\"user\">Username: </label>"
            "<input type=\"text\" id=\"user\" name=\"user\" /></div>\n"
         "<div class=\"login-field\">"
            "<label for=\"password\">Password: </label>"
            "<input type=\"password\" id=\"password\" name=\"password\" />"
               "</div>\n"
   );
   bcgi_check_null( page.text );

   if( NULL != bdata( g_recaptcha_site_key ) ) {
      /* Add recaptcha if key present. */
      retval = bformata( page.text,
         "<div class=\"g-recaptcha\" data-sitekey=\"%s\"></div>\n",
         bdata( g_recaptcha_site_key ) );
      if( BSTR_ERR == retval ) {
         dbglog_error( "unable to allocate profile form!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }

      retval = webutil_add_script( &page,
         "<script src=\"https://www.google.com/recaptcha/api.js\" "
            "async defer></script>\n" );
      if( retval ) {
         goto cleanup;
      }
   }

   retval = bcatcstr( page.text,
      "<div class=\"login-field login-button\">"
         "<input type=\"submit\" name=\"submit\" value=\"Login\" /></div>\n"
      "</form>\n</div>\n" );
   bcgi_check_bstr_err( page.text );

   page.title = &page_title;
   page.flags = WEBUTIL_PAGE_FLAG_NONAV;

   retval = webutil_show_page( &(op->req), q, p, &page );

cleanup:

   bcgi_cleanup_bstr( page.text, likely );
   bcgi_cleanup_bstr( page.scripts, likely );

   return retval;
}

int cchat_auth_user_cb(
   struct WEBUTIL_PAGE* page, struct CCHAT_OP_DATA* op,
   bstring password_test, struct CHATDB_USER* user
) {
   int retval = 0;
   bstring hash_test = NULL;

   assert( NULL != password_test );

   retval = bcgi_hash_password(
      password_test, user->iters, user->hash_sz, user->salt, &hash_test );
   if( retval ) {
      goto cleanup;
   }

   /* Test the provided password. */
   if( 0 != bstrcmp( user->hash, hash_test ) ) {
      retval = RETVAL_AUTH;
      chatdb_free_user( user );
      goto cleanup;
   }

cleanup:

   if( NULL != hash_test ) {
      bdestroy( hash_test );
   }

   return retval;
}

int cchat_route_auth(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   bstring user = NULL;
   bstring user_decode = NULL;
   bstring password = NULL;
   bstring password_decode = NULL;
   bstring err_msg = NULL;
   bstring hash = NULL;
   bstring remote_host = NULL;
   bstring recaptcha = NULL;
   bstring recaptcha_decode = NULL;
   bstring redirect_url = NULL;
   struct CHATDB_USER user_obj;

   memset( &user_obj, '\0', sizeof( struct CHATDB_USER ) );

   dbglog_debug( 1, "route: auth\n" );

   if( NULL == p ) {
      err_msg = bfromcstr( "Invalid message format!" );
      goto cleanup;
   }

   /* Grab recaptcha validation string if applicable. */
   bcgi_query_key( p, "g-recaptcha-response", &recaptcha );
   if( NULL != recaptcha ) {
      retval = bcgi_urldecode( recaptcha, &recaptcha_decode );
      if( retval ) {
         goto cleanup;
      }
      assert( NULL != recaptcha_decode );

      retval = webutil_check_recaptcha( &(op->req), recaptcha_decode );
      if( retval ) {
         assert( NULL == err_msg );
         err_msg = bfromcstr( "Invalid ReCAPTCHA response!" );
         bcgi_check_null( err_msg );
         goto cleanup;
      }
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field( NULL, p, user );
   cchat_decode_field( NULL, p, password );

   /* Validate username and password. */
   user_obj.user_name = bstrcpy( user_decode );
   user_obj.user_id = -1;
   retval = chatdb_iter_users(
      NULL, op, password_decode, &user_obj, cchat_auth_user_cb, &err_msg );
   if( retval || 0 > user_obj.user_id ) {
      if( NULL != err_msg ) {
         retval = bassigncstr( err_msg, "Invalid username or password!" );
         bcgi_check_bstr_err( err_msg );
      } else {
         err_msg = bfromcstr( "Invalid username or password!" );
         bcgi_check_null( err_msg );
      }
      goto cleanup;
   }

   remote_host = bfromcstr( FCGX_GetParam( "REMOTE_ADDR", op->req.envp ) );

   retval = chatdb_add_session(
      op, user_obj.user_id, remote_host, &hash, &err_msg );
   if( retval ) {
      assert( NULL != err_msg );
      goto cleanup;
   }

   dbglog_debug( 2, "setting authorized session cookie: %s\n", bdata( hash ) );

   /* Set auth cookie. */
   FCGX_FPrintF( op->req.out,
      "Set-Cookie: session=%s; Max-Age=%d; HttpOnly\r\n",
      bdata( hash ), user_obj.session_timeout );

cleanup:

   /* Redirect to route. */
   if( NULL != err_msg ) {
      redirect_url = bformat( "/login?error=%s", bdata( err_msg ) );
   } else {
      redirect_url = bfromcstr( "/chat" );
   }
   bcgi_check_null( redirect_url );
   webutil_redirect( &(op->req), redirect_url, 0 );

   bcgi_cleanup_bstr( redirect_url, likely );
   bcgi_cleanup_bstr( remote_host, likely );
   bcgi_cleanup_bstr( hash, likely );
   bcgi_cleanup_bstr( user, likely );
   bcgi_cleanup_bstr( user_decode, likely );
   bcgi_cleanup_bstr( password, likely );
   bcgi_cleanup_bstr( password_decode, likely );
   bcgi_cleanup_bstr( recaptcha, likely );
   bcgi_cleanup_bstr( recaptcha_decode, likely );
   bcgi_cleanup_bstr( err_msg, unlikely );

   chatdb_free_user( &user_obj );

   return retval;

}

int cchat_route_send(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   bstring chat = NULL;
   bstring chat_decode = NULL;
   bstring err_msg = NULL;
   bstring session = NULL;
   bstring csrf = NULL;
   bstring csrf_decode = NULL;
   bstring redirect_url = NULL;

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

   cchat_decode_field( NULL, p, csrf );

   /* Validate CSRF token. */
   if( 0 != bstrcmp( csrf_decode, session ) ) {
      dbglog_error( "invalid csrf!\n" );
      assert( NULL == err_msg );
      err_msg = bfromcstr( "Invalid CSRF token!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* There is POST data, so try to decode it. */
   cchat_decode_field( NULL, p, chat );

   retval = chatdb_send_message( op, auth_user_id, chat_decode, &err_msg );
   if( retval ) {
      goto cleanup;
   }

cleanup:

   /* Redirect to route. */
   if( NULL != err_msg ) {
      /* TODO: Include mini=bottom if frames enabled! */
      redirect_url = bformat( "/chat?error=%s", bdata( err_msg ) );
   } else {
      /* TODO: Don't include mini=bottom if frames not enabled! */
      redirect_url = bfromcstr( "/chat?mini=bottom" );
   }
   bcgi_check_null( redirect_url );
   webutil_redirect( &(op->req), redirect_url, 0 );

   bcgi_cleanup_bstr( redirect_url, likely );
   bcgi_cleanup_bstr( csrf, likely );
   bcgi_cleanup_bstr( csrf_decode, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( chat, likely );
   bcgi_cleanup_bstr( chat_decode, likely );
   bcgi_cleanup_bstr( err_msg, unlikely );

   return retval;
}

int cchat_print_msg_cb(
   struct WEBUTIL_PAGE* page,
   int msg_id, int msg_type, bstring from, int to, bstring text, time_t msg_time
) {
   int retval = 0;
   bstring text_escaped = NULL;
   bstring msg_time_f = NULL;

   /* Sanitize HTML. */
   retval = bcgi_html_escape( text, &text_escaped );
   if( retval ) {
      goto cleanup;
   }

   retval = webutil_format_time( &msg_time_f, msg_time );
   if( retval ) {
      goto cleanup;
   }

   retval = bformata(
      page->text,
      "<tr>"
         "<td class=\"chat-from\">%s</td>"
         "<td class=\"chat-msg\">%s</td>"
         "<td class=\"chat-time\">%s</td>"
      "</tr>\n",
      bdata( from ), bdata( text_escaped ), bdata( msg_time_f ) );
   bcgi_check_bstr_err( page->text );

cleanup:

   bcgi_cleanup_bstr( text_escaped, likely );
   bcgi_cleanup_bstr( msg_time_f, likely );

   return retval;
}

int cchat_route_chat(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;
   struct tagbstring page_title = bsStatic( "Chat" );
   bstring err_msg = NULL;
   bstring session = NULL;
   bstring mini = NULL;
   struct WEBUTIL_PAGE page = { 0, 0, 0 };
   struct CHATDB_USER user;

   page.title = &page_title;
   page.flags = 0;

   memset( &user, '\0', sizeof( struct CHATDB_USER ) );

   if( 0 > auth_user_id ) {
      dbglog_debug( 3, "/chat access by unauthorized user!\n" );

      /* Invalid user; redirect to login. */
      FCGX_FPrintF( op->req.out, "Status: 303 See Other\r\n" );
      FCGX_FPrintF( op->req.out,
         "Location: /login?error=Invalid session cookie!\r\n" );
      FCGX_FPrintF( op->req.out, "Cache-Control: no-cache\r\n" );
      FCGX_FPrintF( op->req.out, "\r\n" );
      goto cleanup;
   }

   /* Grab user properties for preferences. */
   memset( &user, '\0', sizeof( struct CHATDB_USER ) );
   user.user_id = auth_user_id;
   retval = chatdb_iter_users( NULL, op, NULL, &user, NULL, NULL );
   if( retval ) {
      dbglog_error( "error fetching user!\n" );
      goto cleanup;
   }

   page.text = bfromcstr( "" );
   bcgi_check_null( page.text );

   if( CHATDB_USER_FLAG_WS == (CHATDB_USER_FLAG_WS & user.flags) ) {
      /* Add refresher/convenience script. */
      retval = webutil_add_script( &page,
         "<script src=\"https://code.jquery.com/jquery-2.2.4.min.js\" integrity=\"sha256-BbhdlvQf/xTY9gja0Dq3HiwQF8LaCRTXxZKRutelT44=\" crossorigin=\"anonymous\"></script>\n" );
      retval = webutil_add_script( &page,
         "<script type=\"text/javascript\" src=\"chat.js\"></script>\n" );
   }

   /* Show frameset section based on GET string. */
   bcgi_query_key( q, "mini", &mini );
   if( NULL == mini ) {
      mini = bfromcstr( "" );
      bcgi_check_null( mini );
   }

   if(
      CHATDB_USER_FLAG_WS != (CHATDB_USER_FLAG_WS & user.flags) &&
      biseqcaselessStatic( mini, "nav" )
   ) {
      dbglog_debug( 1, "showing nav...\n" );

      /* Only show built-in page nav. */

   } else if(
      CHATDB_USER_FLAG_WS != (CHATDB_USER_FLAG_WS & user.flags) &&
      biseqcaselessStatic( mini, "top" )
   ) {
      dbglog_debug( 1, "showing top frame...\n" );

      /* We're in a frame, so remove decorations. */
      page.flags |= WEBUTIL_PAGE_FLAG_NOTITLE;
      page.flags |= WEBUTIL_PAGE_FLAG_NONAV;

      /* Add auto-refresh. */
      retval = webutil_add_script( &page,
         "<meta http-equiv=\"refresh\" content=\"2\" />\n" );
   
   } else if( 
      CHATDB_USER_FLAG_WS != (CHATDB_USER_FLAG_WS & user.flags) &&
      biseqcaselessStatic( mini, "bottom" )
   ) {
      dbglog_debug( 1, "showing bottom frame...\n" );

      /* Remove page decorations for chat box. */
      page.flags |= WEBUTIL_PAGE_FLAG_NOTITLE;
      page.flags |= WEBUTIL_PAGE_FLAG_NONAV;

   } else if(
      CHATDB_USER_FLAG_WS != (CHATDB_USER_FLAG_WS & user.flags)
   ) {
      dbglog_debug( 1, "showing frameset...\n" );

      /* Show the outside frameset. */
      page.flags |= WEBUTIL_PAGE_FLAG_NOBODY;
      page.flags |= WEBUTIL_PAGE_FLAG_NONAV;
      page.flags |= WEBUTIL_PAGE_FLAG_NOTITLE;

      retval = bassignformat( page.text,
         "<frameset rows=\"10%%,80%%,10%%\">"
            "<frame name=\"top\" src=\"/chat?mini=nav\" />"
            "<frame name=\"main\" src=\"/chat?mini=top\" />"
            "<frame name=\"bottom\" src=\"/chat?mini=bottom\" />"
         "</frameset>" );
      bcgi_check_bstr_err( page.text );
   }

   /* Show messages if we have websockets or are in the message frame. */
   if(
      CHATDB_USER_FLAG_WS == (CHATDB_USER_FLAG_WS & user.flags) ||
      biseqcaselessStatic( mini, "top" )
   ) {
      retval = bcatcstr( page.text, "<table class=\"chat-messages\">\n" );
      bcgi_check_bstr_err( page.text );

      retval = chatdb_iter_messages(
         &page, op, 0, 0, cchat_print_msg_cb, &err_msg );
      if( retval ) {
         dbglog_error( "error iteraing messages!\n" );
         goto cleanup;
      }

      retval = bcatcstr( page.text, "</table>\n" );
      bcgi_check_bstr_err( page.text );
   }

   if(
      CHATDB_USER_FLAG_WS == (CHATDB_USER_FLAG_WS & user.flags) ||
      biseqcaselessStatic( mini, "bottom" )
   ) {
      /* Grab the session for CSRF use. */
      retval = bcgi_query_key( c, "session", &session );
      if( retval || NULL == session ) {
         dbglog_error( "unable to determine session cookie hash!\n" );
         retval = RETVAL_PARAMS;
         goto cleanup;
      }

      /* Show chat input form. */
      retval = bformata(
         page.text,
         "<div class=\"chat-form\">\n"
         "<form action=\"/send\" method=\"post\">\n"
            "<input type=\"text\" id=\"chat\" name=\"chat\" />\n"
            "<input type=\"submit\" name=\"submit\" "
               "id=\"send\" value=\"Send\" />\n"
            "<input type=\"hidden\" name=\"csrf\" value=\"%s\" />\n"
         "</form>\n"
         "</div>\n",
         bdata( session ) );
      bcgi_check_bstr_err( page.text );
   }

   retval = webutil_show_page( &(op->req), q, p, &page );

cleanup:

   chatdb_free_user( &user );

   bcgi_cleanup_bstr( page.scripts, likely );
   bcgi_cleanup_bstr( mini, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( page.text, likely );
   bcgi_cleanup_bstr( err_msg, unlikely );

   return retval;
}

int cchat_route_style_css(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;

   retval = webutil_dump_file( &(op->req), "style.css", "text/css" );

   return retval;
}

int cchat_route_chat_js(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;

   retval = webutil_dump_file( &(op->req), "chat.js", "text/javascript" );

   return retval;
}

int cchat_route_alert_mp3(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;

   retval = webutil_dump_file( &(op->req), "alert.mp3", "audio/mp3" );

   return retval;
}


int cchat_route_root(
   struct CCHAT_OP_DATA* op, int auth_user_id,
   struct bstrList* q, struct bstrList* p, struct bstrList* c
) {
   int retval = 0;

   FCGX_FPrintF( op->req.out, "Status: 303 See Other\r\n" );
   if( 0 > auth_user_id ) {
      /* Invalid user; redirect to login. */
      FCGX_FPrintF( op->req.out, "Location: /login\r\n" );
   } else {
      FCGX_FPrintF( op->req.out, "Location: /chat\r\n" );
   }
   FCGX_FPrintF( op->req.out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( op->req.out, "\r\n" );

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
   struct WEBUTIL_PAGE* page, int* user_id_out_p,
   int session_id, int user_id,
   bstring hash, size_t hash_sz, bstring remote_host, time_t start_time
) {
   int retval = 0;

   dbglog_debug( 2, "set authorized user to: %d\n", user_id );
   *user_id_out_p = user_id;

   return retval;
}

int cchat_handle_req( struct CCHAT_OP_DATA* op ) {
   int retval = 0;
   size_t i = 0;
   bstring req_method = NULL;
   bstring req_uri_raw = NULL;
   bstring req_query = NULL;
   bstring post_buf = NULL;
   bstring session = NULL;
   bstring remote_host = NULL;
   size_t post_buf_sz = 0;
   struct bstrList* req_query_list = NULL;
   struct bstrList* post_buf_list = NULL;
   struct bstrList* req_cookie_list = NULL;
   int auth_user_id = -1;

   if( 0 >= g_dbglog_level ) {
      while( NULL != op->req.envp[i] ) {
         dbglog_debug( 0, "envp: %s\n", op->req.envp[i] );
         i++;
      }
      i = 0;
   }

   remote_host = bfromcstr( FCGX_GetParam( "REMOTE_ADDR", op->req.envp ) );
   dbglog_debug( 1, "remote host: %s\n", bdata( remote_host ) );

   /* Figure out our request method and consequent action. */
   req_method = bfromcstr( FCGX_GetParam( "REQUEST_METHOD", op->req.envp ) );
   bcgi_check_null( req_method );

   req_uri_raw = bfromcstr( FCGX_GetParam( "REQUEST_URI", op->req.envp ) );
   bcgi_check_null( req_uri_raw );

   if( BSTR_ERR != bstrchr( req_uri_raw, '?' ) ) {
      retval = btrunc( req_uri_raw, bstrchr( req_uri_raw, '?' ) );
      bcgi_check_bstr_err( req_uri_raw );
   }

   /* Get query string and split into list. */
   req_query = bfromcstr( FCGX_GetParam( "QUERY_STRING", op->req.envp ) );
   if( NULL == req_query ) {
      dbglog_error( "invalid request query string!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   req_query_list = bsplit( req_query, '&' );

   /* Get cookies and split into list. */
   if( !webutil_get_cookies( &req_cookie_list, op ) ) {
      /* See if a valid session exists (don't urldecode!). */
      retval = bcgi_query_key( req_cookie_list, "session", &session );
      if( !retval && NULL != session ) {
         dbglog_debug( 2, "session cookie found: %s\n", bdata( session ) );
         chatdb_iter_sessions(
            NULL, &auth_user_id, op, session,
            remote_host, cchat_auth_session_cb, NULL );
      }
   }

   /* Get POST data (if any). */
   if( 1 == biseqcaselessStatic( req_method, "POST" ) ) {
      /* Allocate buffer to hold POST data. */
      post_buf_sz = atoi( FCGX_GetParam( "CONTENT_LENGTH", op->req.envp ) );
      post_buf = bfromcstralloc( post_buf_sz + 1, "" );
      if( NULL == post_buf || NULL == bdata( post_buf ) ) {
         dbglog_error( "could not allocate POST buffer!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
      FCGX_GetStr( bdata( post_buf ), post_buf_sz, op->req.in );
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
   i = 0;
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
      dbglog_debug( 1, "found root for URI: %s\n", bdata( req_uri_raw ) );
      /* A valid route was found! */
      retval = gc_cchat_route_cbs[i](
         op, auth_user_id,
         req_query_list, post_buf_list, req_cookie_list );
   } else {
      dbglog_debug(
         1, "did not find root for URI: %s\n", bdata( req_uri_raw ) );
      FCGX_FPrintF( op->req.out, "Status: 404 Bad Request\r\n\r\n" );
   }

cleanup:

   if( NULL != req_cookie_list ) {
      bstrListDestroy( req_cookie_list );
   }

   if( NULL != post_buf_list ) {
      bstrListDestroy( post_buf_list );
   }

   if( NULL != req_query_list ) {
      bstrListDestroy( req_query_list );
   }

   bcgi_cleanup_bstr( remote_host, likely );
   bcgi_cleanup_bstr( post_buf, likely );
   bcgi_cleanup_bstr( session, likely );
   bcgi_cleanup_bstr( req_query, likely );
   bcgi_cleanup_bstr( req_method, likely );
   bcgi_cleanup_bstr( req_uri_raw, likely );
 
   return retval;
}


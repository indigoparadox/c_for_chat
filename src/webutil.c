
#include "main.h"

#ifdef USE_RECAPTCHA

#include <curl/curl.h>
 
bstring g_recaptcha_site_key = NULL;
bstring g_recaptcha_secret_key = NULL;

#endif /* USE_RECAPTCHA */

int webutil_format_time(
   bstring* out_p, bstring time_fmt, int timezone, time_t epoch
) {
   int retval = 0;
   struct tm* ts = NULL;

   assert( NULL == *out_p );

   *out_p = bfromcstralloc( 101, "" );
   bcgi_check_null( *out_p );

   assert( NULL != time_fmt->data );

   ts = localtime( &epoch );
   strftime( (char*)((*out_p)->data), 100, (char*)(time_fmt->data), ts );

cleanup:

   return retval;
}

int webutil_add_script( struct WEBUTIL_PAGE* page, const char* script ) {
   int retval = 0;

   assert( script[strlen( script ) - 1] == '\n' );

   if( NULL == page->scripts ) {
      page->scripts = bfromcstr( script );
   } else {
      retval = bcatcstr( page->scripts, script );
      bcgi_check_bstr_err( page->scripts );
   }

   bcgi_check_null( page->scripts );

cleanup:

   return retval;
}

int webutil_show_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   struct WEBUTIL_PAGE* page
) {
   int retval = 0;
   size_t i = 0;
   bstring err_msg = NULL;
   bstring err_msg_decoded = NULL;
   bstring err_msg_escaped = NULL;

   FCGX_FPrintF( req->out, "Content-type: text/html\r\n" );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "<html>\n" );
   FCGX_FPrintF( req->out, "<head><title>%s</title>\n", bdata( page->title ) );
   FCGX_FPrintF( req->out, "<link rel=\"stylesheet\" href=\"style.css\" />\n" );
   if( NULL != page->scripts ) {
      FCGX_FPrintF( req->out, "%s", bdata( page->scripts ) );
   }
   FCGX_FPrintF( req->out, "</head>\n" );
   if( WEBUTIL_PAGE_FLAG_NOBODY != (WEBUTIL_PAGE_FLAG_NOBODY & page->flags) ) {
      FCGX_FPrintF( req->out, "<body>\n" );
   }
   if(
      WEBUTIL_PAGE_FLAG_NOTITLE != (WEBUTIL_PAGE_FLAG_NOTITLE & page->flags)
   ) {
      FCGX_FPrintF( req->out, "<h1 class=\"page-title\">%s</h1>\n",
         bdata( page->title ) );
   }
   if( WEBUTIL_PAGE_FLAG_NONAV != (WEBUTIL_PAGE_FLAG_NONAV & page->flags) ) {
      FCGX_FPrintF( req->out, "<ul class=\"page-nav\">\n" );
      FCGX_FPrintF( req->out,
         "<li><a target=\"_top\" href=\"/chat\">Chat</a>\n" );
      FCGX_FPrintF( req->out,
         "<li><a target=\"_top\" href=\"/profile\">Profile</a>\n" );
      FCGX_FPrintF( req->out,
         "<li><a target=\"_top\" href=\"/logout\">Logout</a>\n" );
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

   FCGX_FPrintF( req->out, "%s", bdata( page->text ) );

cleanup:

   bcgi_cleanup_bstr( err_msg_decoded, unlikely );
   bcgi_cleanup_bstr( err_msg_escaped, unlikely );
   bcgi_cleanup_bstr( err_msg, unlikely );

   /* Close page. */
   if( WEBUTIL_PAGE_FLAG_NOBODY != (WEBUTIL_PAGE_FLAG_NOBODY & page->flags) ) {
      FCGX_FPrintF( req->out, "</body>\n" );
   }
   FCGX_FPrintF( req->out, "</html>\n" );

   return retval;

}

#ifdef USE_RECAPTCHA

static int webutil_curl_writer(
   char *data, size_t size, size_t nmemb, bstring writer_data
) {
   int retval = 0;

   retval = bcatblk( writer_data, data, size * nmemb );

   /* Translate error into no write. */
   if( BSTR_ERR == retval ) {
      retval = 0;
   } else {
      retval = size * nmemb;
   }

   return retval;
}

int webutil_check_recaptcha( FCGX_Request* req, bstring recaptcha ) {
   int retval = 0;
   bstring recaptcha_curl_post = NULL;
   bstring remote_host = NULL;
   bstring curl_buffer = NULL;
   CURL* curl = NULL;
   CURLcode curl_res;
   const struct tagbstring success_check = bsStatic( "\"success\": true" );

   remote_host = bfromcstr( FCGX_GetParam( "REMOTE_ADDR", req->envp ) );
   bcgi_check_null( remote_host );

   /* Format POST fields for Google. */
   recaptcha_curl_post = bformat( "secret=%s&response=%s&remoteip=%s",
      bdata( g_recaptcha_secret_key ),
      bdata( recaptcha ),
      bdata( remote_host ) );
   bcgi_check_null( recaptcha_curl_post );

   /* Setup CURL handle. */
   curl = curl_easy_init();
   if( !curl ) {
      dbglog_error( "could not init CURL handle!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   curl_buffer = bfromcstr( "" );
   bcgi_check_null( curl_buffer );

   curl_easy_setopt( curl, CURLOPT_URL,
      "https://www.google.com/recaptcha/api/siteverify" );
   curl_easy_setopt( curl, CURLOPT_POSTFIELDS,
      bdata( recaptcha_curl_post ) );
   curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, webutil_curl_writer );
   curl_easy_setopt( curl, CURLOPT_WRITEDATA, curl_buffer );

   curl_res = curl_easy_perform( curl );
   if( CURLE_OK != curl_res ) {
      dbglog_error( "problem verifying recaptcha: %s\n",
         curl_easy_strerror( curl_res ) );
      retval = RETVAL_PARAMS;
   }

   /* XXX */
   dbglog_debug( 1, "%s", bdata( curl_buffer ) );

   /* TODO: Extract/verify response from curl_buffer. */
   if( BSTR_ERR == binstr( curl_buffer, 0, &success_check ) ) {
      dbglog_error( "invalid recaptcha response from %s\n!",
         bdata( remote_host ) );
      retval = RETVAL_PARAMS;
   }

cleanup:

   bcgi_cleanup_bstr( recaptcha_curl_post, likely );
   bcgi_cleanup_bstr( remote_host, likely );

   if( curl ) {
      curl_easy_cleanup( curl );
   }

   return retval;
}

#endif /* USE_RECAPTCHA */

int webutil_get_cookies( struct bstrList** out_p, struct CCHAT_OP_DATA* op ) {
   bstring cookies = NULL;
   int retval = 0;
   size_t i = 0;

   cookies = bfromcstr( FCGX_GetParam( "HTTP_COOKIE", op->req.envp ) );
   if( NULL == cookies ) {
      dbglog_debug( 1, "no cookies present!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   *out_p = bsplit( cookies, ';' );
   bcgi_check_null( *out_p );
   for( i = 0 ; (*out_p)->qty > i ; i++ ) {
      btrimws( (*out_p)->entry[i] );
      dbglog_debug( 1, "cookie: %s\n", bdata( (*out_p)->entry[i] ) );
   }

cleanup:

   bcgi_cleanup_bstr( cookies, likely );

   return retval;
}

int webutil_redirect( FCGX_Request* req, const_bstring url, uint8_t flags ) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Status: 302 Moved Temporarily\r\n" );
   FCGX_FPrintF( req->out, "Location: %s\r\n", bdata( url ) );
   FCGX_FPrintF( req->out, "Cache-Control: no-cache\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   return retval;
}

int webutil_server_error( FCGX_Request *req, const_bstring msg ) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Status: 500 Internal Server Error\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   FCGX_FPrintF( req->out, "<html><head>" );
   FCGX_FPrintF( req->out, "<title>Internal Server Error</title>" );
   FCGX_FPrintF( req->out, "</head><body>" ); 
   FCGX_FPrintF( req->out, "<h1>500: Internal Server Error</h1>" ); 
   if( NULL != msg ) {
      FCGX_FPrintF( req->out, "<p>Message: %s</p>", bdata( msg ) ); 
   }
   FCGX_FPrintF( req->out, "</body></html>" ); 

   return retval;
}

int webutil_unauthorized( FCGX_Request *req ) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Status: 401 Unauthorized\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   FCGX_FPrintF( req->out, "<html><head>" );
   FCGX_FPrintF( req->out, "<title>Unauthorized</title>" );
   FCGX_FPrintF( req->out, "</head><body>" ); 
   FCGX_FPrintF( req->out, "<h1>401 Unauthorized</h1>" ); 
   FCGX_FPrintF(
      req->out, "<p>Please try <a href=\"/login\">logging in</a>!</p>" );
   FCGX_FPrintF( req->out, "</body></html>" ); 

   return retval;
}

int webutil_not_found( FCGX_Request *req ) {
   int retval = 0;

   FCGX_FPrintF( req->out, "Status: 404 Not Found\r\n" );
   FCGX_FPrintF( req->out, "\r\n" ); 

   FCGX_FPrintF( req->out, "<html><head>" );
   FCGX_FPrintF( req->out, "<title>Not Found</title>" );
   FCGX_FPrintF( req->out, "</head><body>" ); 
   FCGX_FPrintF( req->out, "<h1>404 Not Found</h1>" ); 
   FCGX_FPrintF(
      req->out, "<p>Continue to <a href=\"/login\">login</a>...</p>" );
   FCGX_FPrintF( req->out, "</body></html>" ); 

   return retval;
}

int webutil_form_field_bstring(
   bstring html, const char* name, bstring* val
) {
   int retval = 0;

   retval = bformata( html,
      "<div class=\"profile-field\">"
         "<label for=\"%s\">%s: </label>"
         "<input type=\"text\" id=\"%s\" name=\"%s\" value=\"%s\" />"
            "</div>\n",
      name, name, name, name,
      NULL != val && NULL != *val ? bdata( *val ) : "" );
   bcgi_check_bstr_err( html );

cleanup:

   return retval;
}

int webutil_form_field_int( bstring html, const char* name, int* val ) {
   int retval = 0;

   retval = bformata( html,
      "<div class=\"profile-field\">"
         "<label for=\"%s\">%s: </label>"
         "<input type=\"text\" id=\"%s\" name=\"%s\" value=\"%d\" />"
            "</div>\n",
      name, name, name, name,
      NULL != val ? *val : 0 );
   bcgi_check_bstr_err( html );

cleanup:

   return retval;
}

int webutil_form_field_time_t( bstring html, const char* name, time_t* val ) {
   int retval = 0;

   retval = bformata( html,
      "<div class=\"profile-field\">"
         "<label for=\"%s\">%s: </label>"
         "<input type=\"text\" id=\"%s\" name=\"%s\" value=\"%d\" />"
            "</div>\n",
      name, name, name, name,
      NULL != val ? *val : 0 );
   bcgi_check_bstr_err( html );

cleanup:

   return retval;
}


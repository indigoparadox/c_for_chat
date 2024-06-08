
#include "main.h"

#include <curl/curl.h>

int webutil_format_time( bstring* out_p, time_t epoch ) {
   int retval = 0;
   struct tm* ts = NULL;

   assert( NULL == *out_p );

   *out_p = bfromcstralloc( 101, "" );
   bcgi_check_null( *out_p );

   ts = localtime( &epoch );
   strftime( (char*)((*out_p)->data), 100, "%Y-%m-%d %H:%M %Z", ts );

cleanup:

   return retval;
}

int webutil_dump_file(
   FCGX_Request* req, const char* filename, const char* mimetype
) {
   int retval = 0;
   FILE* fp = NULL;
   bstring contents = NULL;

   fp = fopen( filename, "rb");
   if( NULL == fp ) {
      dbglog_error( "couldn't open file %s!\n", filename );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   contents = bread( (bNread)fread, fp );
   if( NULL == contents ) {
      dbglog_error( "couldn't read file %s!\n", filename );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   FCGX_FPrintF( req->out, "Content-type: %s\r\n", mimetype );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

   FCGX_FPrintF( req->out, "%s", bdata( contents ) );

cleanup:

   if( NULL != fp ) {
      fclose( fp );
   }

   bcgi_cleanup_bstr( contents, likely );

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
   if( WEBUTIL_PAGE_FLAG_NOBODY != (WEBUTIL_PAGE_FLAG_NOBODY & page->flags) ) {
      FCGX_FPrintF( req->out, "</body>\n" );
   }
   FCGX_FPrintF( req->out, "</html>\n" );

   return retval;

}

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
   bstring recaptcha_secret_key = NULL;
   bstring recaptcha_curl_post = NULL;
   bstring remote_host = NULL;
   bstring curl_buffer = NULL;
   CURL* curl = NULL;
   CURLcode curl_res;
   const struct tagbstring success_check = bsStatic( "\"success\": true" );

   recaptcha_secret_key = bfromcstr(
      FCGX_GetParam( "CCHAT_RECAPTCHA_SECRET", req->envp ) );
   bcgi_check_null( recaptcha_secret_key );

   remote_host = bfromcstr( FCGX_GetParam( "REMOTE_ADDR", req->envp ) );
   bcgi_check_null( remote_host );

   /* Format POST fields for Google. */
   recaptcha_curl_post = bformat( "secret=%s&response=%s&remoteip=%s",
      bdata( recaptcha_secret_key ),
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

   bcgi_cleanup_bstr( recaptcha_secret_key, likely );
   bcgi_cleanup_bstr( recaptcha_curl_post, likely );
   bcgi_cleanup_bstr( remote_host, likely );

   if( curl ) {
      curl_easy_cleanup( curl );
   }

   return retval;
}


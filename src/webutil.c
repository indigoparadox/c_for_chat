
#include "main.h"

#include <curl/curl.h>

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


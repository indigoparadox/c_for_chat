
#include "main.h"

#include "../assets/style.css.h"
#include "../assets/alert.mp3.h"
#include "../assets/chat.js.h"
#include "../assets/strftime.js.h"

#define ASSETS_TABLE( f ) \
   f( "style.css", style_css ) \
   f( "alert.mp3", alert_mp3 ) \
   f( "chat.js", chat_js ) \
   f( "strftime.js", strftime_js )

#define ASSETS_TABLE_NAMES( name, ptr ) bsStatic( name ),

const static struct tagbstring gc_assets_names[] = {
   ASSETS_TABLE( ASSETS_TABLE_NAMES )
};

#define ASSETS_TABLE_PTRS( name, ptr ) ptr,

const static unsigned char* gc_assets_ptrs[] = {
   ASSETS_TABLE( ASSETS_TABLE_PTRS )
   NULL
};

#define ASSETS_TABLE_SZS( name, ptr ) &ptr ## _len,

const static unsigned int* gc_assets_szs[] = {
   ASSETS_TABLE( ASSETS_TABLE_SZS )
};

int assets_dump_file(
   FCGX_Request* req, const_bstring filename, const_bstring mimetype
) {
   int retval = 0;
   size_t i = 0;

#ifdef ASSETS_FILES
   FILE* fp = NULL;
   bstring contents = NULL;

   fp = fopen( bdata( filename ), "rb");
   if( NULL == fp ) {
      dbglog_error( "couldn't open file %s!\n", bdata( filename ) );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   contents = bread( (bNread)fread, fp );
   if( NULL == contents ) {
      dbglog_error( "couldn't read file %s!\n", bdata( filename ) );
      retval = RETVAL_FILE;
      goto cleanup;
   }

   dbglog_debug( 1, "%s: %d bytes\n", bdata( filename ), blength( contents ) );
#else
   for( i = 0 ; NULL != gc_assets_ptrs[i] ; i++ ) {
      if( 0 == bstrcmp( filename, &(gc_assets_names[i]) ) ) {
         break;
      }
   }

   assert( NULL != gc_assets_ptrs[i] );
#endif

   FCGX_FPrintF( req->out, "Content-type: %s\r\n", bdata( mimetype ) );
   FCGX_FPrintF( req->out, "Status: 200\r\n\r\n" );

#ifdef ASSETS_FILES
   FCGX_PutStr( bdata( contents ), blength( contents ), req->out );
#else
   FCGX_PutStr( (const char*)gc_assets_ptrs[i], *(gc_assets_szs[i]), req->out );
#endif /* ASSETS_FILES */

cleanup:

#ifdef ASSETS_FILES
   if( NULL != fp ) {
      fclose( fp );
   }

   bcgi_cleanup_bstr( contents, likely );
#endif /* ASSETS_FILES */

   return retval;
}



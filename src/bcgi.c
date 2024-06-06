
#define BCGI_C
#include "bcgi.h"

#include "dbglog.h"

int bcgi_query_key( struct bstrList* array, const char* key, bstring* val_p ) {
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
         dbglog_error( "could not allocate value bstring!\n" );
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



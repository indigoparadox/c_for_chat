
#define BCGI_C
#include "bcgi.h"

#include "dbglog.h"

#define URLDECODE_STATE_NONE     0
#define URLDECODE_STATE_INSIDE   1

int bcgi_urldecode( bstring in, bstring* out_p ) {
   int retval = 0,
      state = 0;
   size_t i_in = 0;
   bstring decode_buf = NULL;

   decode_buf = bfromcstr( "" );
   
   if( NULL == *out_p ) {
      *out_p = bfromcstr( "" );
      if( NULL == *out_p ) {
         dbglog_error( "could not allocate urldecode output buffer!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   while( i_in < blength( in ) ) {
      switch( state ) {
      case URLDECODE_STATE_INSIDE:
         if( ';' == bchar( in, i_in ) ) {
            /* TODO: Perform decode of decode_buf. */
            dbglog_debug( 1, "decoding entity: %s", bdata( decode_buf ) );

            /* Reset the decode buffer. */
            retval = bassignStatic( decode_buf, "" );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_PARAMS;
               goto cleanup;
            }

            state = URLDECODE_STATE_NONE;

         } else if( !bcgi_is_digit( bchar( in, i_in ) ) ) {
            /* Give up on decoding this! Just pass it literally. */

            /* The & we discarded earlier. */
            retval = bconchar( *out_p, '&' );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_PARAMS;
               goto cleanup;
            }
            /* The decode_buf so far. */
            retval = bconcat( *out_p, decode_buf );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_PARAMS;
               goto cleanup;
            }
            state = URLDECODE_STATE_NONE;

         } else {
            /* Concat the number to the decode buf. */
            retval = bconchar( decode_buf, bchar( in, i_in ) );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_PARAMS;
               goto cleanup;
            }

         }
         break;

      case URLDECODE_STATE_NONE:
         retval = bconchar( *out_p, bchar( in, i_in ) );
         break;
      }
      i_in++;
   }

   /* We finished without hitting a goto. */
   retval = 0;

cleanup:

   if( NULL != decode_buf ) {
      bdestroy( decode_buf );
   }

   return retval;
}

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



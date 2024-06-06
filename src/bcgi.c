
#define BCGI_C
#include "main.h"

#include <stdlib.h> /* for atoi() */

#define URLDECODE_STATE_NONE     0
#define URLDECODE_STATE_INSIDE   1

static int _bcgi_urldecode_entity( bstring out, bstring decode ) {
   int retval = 0;

   dbglog_debug( 1, "decoding entity: %s", bdata( decode ) );

   retval = bconchar( out, (char)strtol( (char*)decode->data, NULL, 16 ) );
   if( BSTR_ERR == retval ) {
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   /* Reset the decode buffer. */
   retval = bassignStatic( decode, "" );
   if( BSTR_ERR == retval ) {
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

cleanup:

   return retval;
}

int bcgi_urldecode( bstring in, bstring* out_p ) {
   int retval = 0,
      state = 0;
   size_t i_in = 0;
   bstring decode_buf = NULL;

   decode_buf = bfromcstr( "" );
   if( NULL == decode_buf ) {
      dbglog_error( "could not allocate urldecode decode buffer!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
   
   if( NULL == *out_p ) {
      *out_p = bfromcstr( "" );
      if( NULL == *out_p ) {
         dbglog_error( "could not allocate urldecode output buffer!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }

   while( i_in < blength( in ) ) {
      dbglog_debug( 1, "state: %d, char: %c\n", state, bchar( in, i_in ) );
      switch( state ) {
      case URLDECODE_STATE_INSIDE:
         if(
            !bcgi_is_hex_digit( bchar( in, i_in ) ) ||
            /* TODO: Always limit decode buf to two chars? */
            2 <= blength( decode_buf )
         ) {
            /* TODO: Perform decode of decode_buf. */
            _bcgi_urldecode_entity( *out_p, decode_buf );
            state = URLDECODE_STATE_NONE;

            /* Don't move on to the next char; process this one again with the
             * new state!
             */

         } else {
            /* Concat the number to the decode buf. */
            retval = bconchar( decode_buf, bchar( in, i_in ) );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_PARAMS;
               goto cleanup;
            }

            /* Move on to the next char. */
            i_in++;
         }
         break;

      case URLDECODE_STATE_NONE:
         switch( bchar( in, i_in ) ) {
         case '+':
            /* Translate plus to space. */
            retval = bconchar( *out_p, ' ' );
            break;

         case '%':
            /* Entity found! */
            state = URLDECODE_STATE_INSIDE;
            break;

         default:
            /* Literal char. */
            retval = bconchar( *out_p, bchar( in, i_in ) );
            break;
         }
         
         /* Move on to the next char. */
         i_in++;
         break;
      }
   }

   if( URLDECODE_STATE_INSIDE == state ) {
      /* The string must've ended with an entity. */
      _bcgi_urldecode_entity( *out_p, decode_buf );
   }

   /* We finished without hitting a goto. */
   retval = 0;

cleanup:

   if( NULL != decode_buf ) {
      bdestroy( decode_buf );
   }

   return retval;
}

int bcgi_html_escape( bstring in, bstring* out_p ) {
   int retval = 0;
   size_t i = 0;

   *out_p = bstrcpy( in );
   if( NULL == *out_p ) {
      dbglog_error( "could not allocate replacement string!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   while( 0 < blength( &(gc_html_esc_before[i]) ) ) {
      retval = bfindreplacecaseless(
         *out_p, &(gc_html_esc_before[i]), &(gc_html_esc_after[i]), 0 );
      if( BSTR_ERR == retval ) {
         retval = RETVAL_ALLOC;
         dbglog_error( "error escaping HTML!\n" );
         goto cleanup;
      } else {
         retval = 0;
      }
      i++;
   }

cleanup:

   if( retval && NULL != *out_p ) {
      /* If there was a problem then don't risk a non-decoded message! */
      bdestroy( *out_p );
      *out_p = NULL;
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



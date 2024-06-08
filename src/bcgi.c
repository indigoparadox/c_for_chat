
#define BCGI_C
#include "main.h"

#include <stdlib.h> /* for atoi() */

#define URLDECODE_STATE_NONE     0
#define URLDECODE_STATE_INSIDE   1

static int _bcgi_urldecode_entity( bstring out, bstring decode ) {
   int retval = 0;

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
               retval = RETVAL_ALLOC;
               dbglog_error( "error appending to decode buffer!\n" );
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
            if( BSTR_ERR == retval ) {
               retval = RETVAL_ALLOC;
               dbglog_error( "error appending to output buffer!\n" );
               goto cleanup;
            }
            break;

         case '%':
            /* Entity found! */
            state = URLDECODE_STATE_INSIDE;
            break;

         default:
            /* Literal char. */
            retval = bconchar( *out_p, bchar( in, i_in ) );
            if( BSTR_ERR == retval ) {
               retval = RETVAL_ALLOC;
               dbglog_error( "error appending to output buffer!\n" );
               goto cleanup;
            }
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

#define BCGI_JSON_PARSE_STATE_STACK_DEPTH    10

#define BCGI_JSON_PARSE_STATE_NONE        0
#define BCGI_JSON_PARSE_STATE_OBJ         1
#define BCGI_JSON_PARSE_STATE_OBJ_KEY     2
#define BCGI_JSON_PARSE_STATE_OBJ_VAL     3
#define BCGI_JSON_PARSE_STATE_OBJ_VAL_STR 3

#define bcgi_parse_json_push_state( s ) \
   state[++state_depth] = BCGI_JSON_PARSE_STATE_ ## s

#define bcgi_parse_json_state() \
   (bcgi_likely( state_depth > 0 ) ? state[state_depth] : 0)

#define bcgi_parse_json_pop_state_expect( s ) \
   state_depth--; \
   if( \
      bcgi_unlikely( 0 <= state_depth ) && \
      bcgi_unlikely( BCGI_JSON_PARSE_STATE_ ## s != state[state_depth] ) \
   ) { \
      dbglog_error( "parse error: expected " #s " but got %d!\n", \
         state[state_depth] ); \
      retval = RETVAL_PARAMS; \
      goto cleanup; \
   }

int bcgi_parse_json( struct BCGI_JSON_NODE** root_p, bstring buffer ) {
   size_t i = 0;
   int retval = 0;
   char c = 0;
   bstring token = NULL;
   int state[BCGI_JSON_PARSE_STATE_STACK_DEPTH] = { 0 };
   int state_depth = -1; /* Weird type of index to avoid math on inner loop. */

   token = bfromcstr( "" );
   bcgi_check_null( token );

   for( i = 0 ; blength( buffer ) > i ; i++ ) {
      c = bchar( buffer, i );
      
      switch( c ) {
      case '{':
         switch( bcgi_parse_json_state() ) {
         case BCGI_JSON_PARSE_STATE_NONE:
            /* Start object. */
            bcgi_parse_json_push_state( OBJ );
            break;

         case BCGI_JSON_PARSE_STATE_OBJ_VAL_STR:
            /* Append literal to string being built. */
            retval = bconchar( token, c );
            bcgi_check_bstr_err( token );
            break;
         }
         break;

      case '}':
         switch( bcgi_parse_json_state() ) {
         case BCGI_JSON_PARSE_STATE_NONE:
            /* Start object. */
            bcgi_parse_json_pop_state_expect( OBJ );
            break;

         case BCGI_JSON_PARSE_STATE_OBJ_VAL_STR:
            /* Append literal to string being built. */
            retval = bconchar( token, c );
            bcgi_check_bstr_err( token );
            break;
         }
         break;

      default:
         retval = bconchar( token, c );
         bcgi_check_bstr_err( token );
         break;
      }
   }

cleanup:

   bcgi_cleanup_bstr( token, bcgi_unlikely );

   return retval;
}


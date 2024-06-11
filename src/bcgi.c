
#define BCGI_C
#include "main.h"

#include <stdlib.h> /* for atoi() */

#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

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
      bcgi_check_null( *out_p );
   } else {
      retval = btrunc( *out_p, 0 );
      bcgi_check_bstr_err( *out_p );
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

int bcgi_query_key_str(
   bstring list_str, char split_c, const char* key, bstring* val_p
) {
   int retval = 0;
   struct bstrList* list = NULL;
   size_t i = 0;
   
   /* Split the cookie string. */
   list = bsplit( list_str, split_c );
   bcgi_check_null( list );
   dbglog_debug( 1, "split list \"%s\" (%d) on \"%c\": %d entries\n",
      bdata( list_str ), blength( list_str ), split_c, list->qty );
   for( i = 0 ; list->qty > i ; i++ ) {
      btrimws( list->entry[i] );
      dbglog_debug( 1, "entry %d: %s\n", i, bdata( list->entry[i] ) );
   }

   retval = bcgi_query_key( list, key, val_p );

cleanup:

   if( NULL != list ) {
      bstrListDestroy( list );
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
      bcgi_check_null( *val_p );
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

   bcgi_cleanup_bstr( token, likely );

   return retval;
}

int bcgi_hash_sha( bstring in, bstring* out_p ) {
   int retval = 0;
   SHA_CTX hash_ctx;
   unsigned char out_c[SHA_DIGEST_LENGTH];

   assert( NULL == *out_p );

   /* Generate the hash. */
   if( !SHA1_Init( &hash_ctx ) ) {
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   if( !SHA1_Update(
      &hash_ctx, (unsigned char*)bdata( in ), blength( in )
   ) ) {
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   if( !SHA1_Final( out_c, &hash_ctx ) ) {
      retval = RETVAL_SOCK;
      goto cleanup;
   }

   retval = bcgi_b64_encode( out_c, SHA_DIGEST_LENGTH, out_p );

cleanup:

   return retval;
}

int bcgi_b64_decode( bstring in, unsigned char** out_p, size_t* out_sz_p ) {
   int retval = 0;
   size_t decoded_sz = 0;

   /* Base64-decode the salt. */
   *out_sz_p = 3 * blength( in ) / 4;
   *out_p = calloc( *out_sz_p, 1 );
   if( NULL == *out_p ) {
      dbglog_error( "could not allocate decoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   decoded_sz = EVP_DecodeBlock(
      *out_p, (unsigned char*)bdata( in ), blength( in ) );
   if( decoded_sz != *out_sz_p ) {
      dbglog_error( "predicted sz: %lu but was: %lu\n",
         *out_sz_p, decoded_sz );
   }

cleanup:

   return retval;
}

int bcgi_b64_encode( unsigned char* in, size_t in_sz, bstring* out_p ) {
   int retval = 0;
   unsigned char* str = NULL;
   size_t str_sz = 0;
   size_t encoded_sz = 0;

   /* Base64-encode the hash. */
   str_sz = 4 * ((in_sz + 2) / 3);
   str = calloc( str_sz + 1, 1 );
   if( NULL == str ) {
      dbglog_error( "could not allocate hash encoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   encoded_sz = EVP_EncodeBlock( str, in, in_sz );
   if( encoded_sz != str_sz ) {
      dbglog_error( "predicted hash sz: %lu but was: %lu\n",
         str_sz, encoded_sz );
   }

   *out_p = bfromcstr( (char*)str );
   if( NULL == *out_p ) {
      dbglog_error( "could not allocate encoded bstring!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
 
cleanup:

   if( NULL != str ) {
      free( str );
   }

   return retval;
}

int bcgi_hash_password(
   bstring password, size_t password_iter, size_t hash_sz,
   bstring salt, bstring* hash_out_p
) {
   unsigned char hash_bin[SHA256_DIGEST_LENGTH] = { 0 };
   size_t salt_bin_sz = 0;
   int retval = 0;
   unsigned char* salt_bin = NULL;

   retval = bcgi_b64_decode( salt, &salt_bin, &salt_bin_sz );
   if( retval ) {
      goto cleanup;
   }

   /* Generate the hash. */
   if( !PKCS5_PBKDF2_HMAC(
      bdata( password ), blength( password ),
      salt_bin, salt_bin_sz, password_iter, EVP_sha256(),
      SHA256_DIGEST_LENGTH, hash_bin )
   ) {
      dbglog_error( "error generating password hash!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* TODO: Push hash size in DB. */
   retval = bcgi_b64_encode( hash_bin, SHA256_DIGEST_LENGTH, hash_out_p );
   if( retval ) {
      goto cleanup;
   }

cleanup:

   if( NULL != salt_bin ) {
      free( salt_bin );
   }

   return retval;
}

int bcgi_generate_salt( bstring* out_p, size_t salt_sz ) {
   int retval = 0;
   unsigned char* salt_bin = NULL;

   assert( NULL == *out_p );

   salt_bin = calloc( salt_sz, 1 );
   bcgi_check_null( salt_bin );

   if( 1 != RAND_bytes( salt_bin, salt_sz ) ) {
      dbglog_error( "error generating random bytes!\n" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   retval = bcgi_b64_encode( salt_bin, salt_sz, out_p );
   if( retval ) {
      goto cleanup;
   }

cleanup:

   if( NULL != salt_bin ) {
      free( salt_bin );
   }

   return retval;
}



#ifndef BCGI_H
#define BCGI_H

#define CCHAT_HTML_ESC_TABLE( f ) \
   f( "<", "&lt;", CSTR_LT ) \
   f( ">", "&gt;", CSTR_GT ) \
   f( "", "", CSTR_MAX )

#define BCGI_JSON_NODE_TYPE_TEXT   0x01
#define BCGI_JSON_NODE_TYPE_INT    0x02
#define BCGI_JSON_NODE_TYPE_OBJECT 0x03
#define BCGI_JSON_NODE_TYPE_LIST   0x04

struct BCGI_JSON_NODE {
   int type;
};

struct BCGI_JSON_NODE_INT {
   struct BCGI_JSON_NODE base;
   int value;
};

struct BCGI_JSON_NODE_TEXT {
   struct BCGI_JSON_NODE base;
   bstring value;
};

struct BCGI_JSON_NODE_OBJECT {
   struct BCGI_JSON_NODE base;
   bstring key;
   struct BCGI_JSON_NODE* value;
   struct BCGI_JSON_NODE* next;
};

struct BCGI_JSON_NODE_LIST {
   struct BCGI_JSON_NODE base;
   struct BCGI_JSON_NODE* value;
   struct BCGI_JSON_NODE* next;
};

#define bcgi_likely( x ) __builtin_expect( !!( x ), 1 ) 
#define bcgi_unlikely( x ) __builtin_expect( !!( x ), 0 ) 

#define bcgi_is_digit( c ) ((c) > 0x2f && (c) < 0x40)

#define bcgi_is_hex_digit( c ) \
   (bcgi_is_digit( c ) || ((c) > 0x40 && (c) < 0x47))

#define bcgi_check_null( p ) \
   if( NULL == p ) { \
      dbglog_error( #p " was NULL!\n" ); \
      retval = RETVAL_ALLOC; \
      goto cleanup; \
   }

#define bcgi_cleanup_bstr( b, l ) \
   if( bcgi_ ## l( NULL != b ) ) { \
      bdestroy( b ); \
      b = NULL; \
   }

#define bcgi_check_bstr_err( b ); \
   if( bcgi_unlikely( BSTR_ERR == retval ) ) { \
      dbglog_error( "error during operation on " #b "!\n" ); \
      retval = RETVAL_ALLOC; \
      goto cleanup; \
   }

int bcgi_urldecode( bstring in, bstring* out_p );

int bcgi_html_escape( bstring in, bstring* out_p );

int bcgi_query_key_str(
   bstring list_str, char split, const char* key, bstring* val_p );

int bcgi_query_key( struct bstrList* array, const char* key, bstring* val_p );

int bcgi_b64_decode( bstring in, unsigned char** out_p, size_t* out_sz_p );

int bcgi_b64_encode( unsigned char* in, size_t in_sz, bstring* out_p );

int bcgi_hash_password(
   bstring password, size_t password_iter, size_t hash_sz,
   bstring salt, bstring* hash_out_p );

int bcgi_generate_salt( bstring* out_p, size_t salt_sz );

#ifdef BCGI_C

#define CCHAT_HTML_ESC_TABLE_STR( str, esc, id ) bsStatic( str ),

static struct tagbstring gc_html_esc_before[] = {
   CCHAT_HTML_ESC_TABLE( CCHAT_HTML_ESC_TABLE_STR )
};

#define CCHAT_HTML_ESC_TABLE_ESC( str, esc, id ) bsStatic( esc ),

static struct tagbstring gc_html_esc_after[] = {
   CCHAT_HTML_ESC_TABLE( CCHAT_HTML_ESC_TABLE_ESC )
};

#endif /* BCGI_C */

#endif /* !BCGI_H */


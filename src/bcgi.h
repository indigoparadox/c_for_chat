
#ifndef BCGI_H
#define BCGI_H

#define CCHAT_HTML_ESC_TABLE( f ) \
   f( "<", "&lt;", CSTR_LT ) \
   f( ">", "&gt;", CSTR_GT ) \
   f( "", "", CSTR_MAX )

#define bcgi_is_digit( c ) ((c) > 0x2f && (c) < 0x40)

#define bcgi_is_hex_digit( c ) \
   (bcgi_is_digit( c ) || ((c) > 0x40 && (c) < 0x47))

int bcgi_urldecode( bstring in, bstring* out_p );

int bcgi_html_escape( bstring in, bstring* out_p );

int bcgi_query_key( struct bstrList* array, const char* key, bstring* val_p );

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


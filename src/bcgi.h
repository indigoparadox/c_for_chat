
#ifndef BCGI_H
#define BCGI_H

#include "bstrlib.h"

#define CCHAT_HTML_ESC_TABLE( f ) \
   f( "<", "&lt;", CSTR_LT ) \
   f( ">", "&gt;", CSTR_GT ) \
   f( "", "", CSTR_MAX )

int bcgi_query_key( struct bstrList* array, const char* key, bstring* val_p );

#ifdef BCGI_C

#define CCHAT_HTML_ESC_TABLE_STR( str, esc, id ) bsStatic( str ),

struct tagbstring gc_html_esc_before[] = {
   CCHAT_HTML_ESC_TABLE( CCHAT_HTML_ESC_TABLE_STR )
};

#define CCHAT_HTML_ESC_TABLE_ESC( str, esc, id ) bsStatic( esc ),

struct tagbstring gc_html_esc_after[] = {
   CCHAT_HTML_ESC_TABLE( CCHAT_HTML_ESC_TABLE_ESC )
};

#else

extern struct tagbstring gc_html_esc_before[];

extern struct tagbstring gc_html_esc_after[];

#endif /* BCGI_C */

#endif /* !BCGI_H */


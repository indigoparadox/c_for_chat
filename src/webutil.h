
#ifndef WEBUTIL_H
#define WEBUTIL_H

#define WEBUTIL_PAGE_FLAG_NONAV     0x01

#define WEBUTIL_PAGE_FLAG_NOTITLE   0x02

#define WEBUTIL_PAGE_FLAG_NOBODY    0x04

struct WEBUTIL_PAGE {
   uint8_t flags;
   bstring text;
   bstring title;
   bstring scripts;
};

int webutil_format_time(
   bstring* out_p, bstring time_fmt, int timezone, time_t epoch );

int webutil_dump_file(
   FCGX_Request* req, const char* filename, const char* mimetype );

int webutil_show_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   struct WEBUTIL_PAGE* page );

int webutil_add_script( struct WEBUTIL_PAGE* page, const char* script );

int webutil_check_recaptcha( FCGX_Request* req, bstring recaptcha );

int webutil_get_cookies( struct bstrList** out_p, struct CCHAT_OP_DATA* op );

int webutil_redirect( FCGX_Request* req, const_bstring url, uint8_t flags );

int webutil_server_error( FCGX_Request *req, bstring msg );

int webutil_not_found( FCGX_Request *req );

int webutil_form_field_bstring( bstring html, const char* name, bstring* val );

int webutil_form_field_int( bstring html, const char* name, int* val );

int webutil_form_field_time_t( bstring html, const char* name, time_t* val );

#endif /* !WEBUTIL_H */


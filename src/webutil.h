
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

int webutil_format_time( bstring* out_p, time_t epoch );

int webutil_dump_file(
   FCGX_Request* req, const char* filename, const char* mimetype );

int webutil_show_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   struct WEBUTIL_PAGE* page );

int webutil_add_script( struct WEBUTIL_PAGE* page, const char* script );

int webutil_check_recaptcha( FCGX_Request* req, bstring recaptcha );

#endif /* !WEBUTIL_H */


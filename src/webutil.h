
#ifndef WEBUTIL_H
#define WEBUTIL_H

#define WEBUTIL_PAGE_FLAG_NONAV    0x01

struct WEBUTIL_PAGE {
   bstring text;
   bstring title;
   bstring scripts;
};

int webutil_show_page(
   FCGX_Request* req, struct bstrList* q, struct bstrList* p,
   struct WEBUTIL_PAGE* page, uint8_t flags );

int webutil_add_script( struct WEBUTIL_PAGE* page, const char* script );

int webutil_check_recaptcha( FCGX_Request* req, bstring recaptcha );

#endif /* !WEBUTIL_H */


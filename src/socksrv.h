
#ifndef SOCKSRV_H
#define SOCKSRV_H

#define SOCKSRV_BUF_SZ_MAX 4000

#define SOCKSRV_STAGE_MAX 10

struct SOCKSRV_SERVER;

typedef int (*socksrv_client_handler)( struct SOCKSRV_SERVER*, bstring );

struct SOCKSRV_HEADERS {
   struct bstrList* keys;
   struct bstrList* values;
};

struct SOCKSRV_SERVER {
   socksrv_client_handler handlers[SOCKSRV_STAGE_MAX];
   size_t stage;
   int server;
   int client;
   struct SOCKSRV_HEADERS h;
};

int socksrv_get_header(
   bstring key, bstring* value_p, struct SOCKSRV_HEADERS* h );

int socksrv_parse_headers( bstring buffer, struct SOCKSRV_HEADERS* h );

int socksrv_listen();

void socksrv_stop();

#endif /* !SOCKSRV_H */


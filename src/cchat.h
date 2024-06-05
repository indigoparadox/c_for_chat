
#ifndef CCHAT_H
#define CCHAT_H

#include <fcgi_stdio.h>

#include "bstrlib.h"
#include "retval.h"

int cchat_handle_req( FCGX_Request* req, sqlite3* db );

#endif /* !CCHAT_H */


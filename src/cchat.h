
#ifndef CCHAT_H
#define CCHAT_H

#include <fcgi_stdio.h>

#include "bstrlib.h"

#define RETVAL_PARAMS 1
#define RETVAL_METHOD 2

int cchat_handle_req( FCGX_Request* req );

#endif /* !CCHAT_H */


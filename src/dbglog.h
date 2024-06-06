
#ifndef DBGLOG_H
#define DBGLOG_H

#include "retval.h"

int dbglog_init( char* dbglog_path );

void dbglog_shutdown();

#define _dbglog_fmt( lvl, file, line ) "(" #lvl ") " file ": " #line ": "

#define dbglog_debug( lvl, ... ) \
   fprintf( g_dbglog_file, _dbglog_fmt( lvl, __FILE__, __LINE__ ) __VA_ARGS__ ); \
   fflush( g_dbglog_file );

#define dbglog_error( ... ) \
   fprintf( g_dbglog_file, _dbglog_fmt( E, __FILE__, __LINE__ ) __VA_ARGS__ ); \
   fflush( g_dbglog_file );

#ifdef DBGLOG_C

FILE* g_dbglog_file = NULL;

int dbglog_init( char* dbglog_path ) {
   int retval = 0;

   g_dbglog_file = fopen( dbglog_path, "w" );
   if( NULL == g_dbglog_file ) {
      retval = RETVAL_FILE;
   }

   return retval;
}

void dbglog_shutdown() {
   fclose( g_dbglog_file );
}

#else

extern FILE* g_dbglog_file;

#endif /* DBGLOG_C */

#endif /* !DBGLOG_H */


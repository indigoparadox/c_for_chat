
#ifndef DBGLOG_H
#define DBGLOG_H

/* int dbglog_init( char* dbglog_path );

void dbglog_shutdown(); */

#define _ds( s ) #s

#define _dbglog_fmt( lvl, file, line ) "(" #lvl ") " file ": " _ds( line ) ": "

#define dbglog_debug( lvl, ... ) \
   if( NULL != g_dbglog_file && lvl >= g_dbglog_level ) { \
      pthread_mutex_lock( &g_dbglog_lock ); \
      fprintf( \
         g_dbglog_file, _dbglog_fmt( lvl, __FILE__, __LINE__ ) __VA_ARGS__ ); \
      fflush( g_dbglog_file ); \
      pthread_mutex_unlock( &g_dbglog_lock ); \
   }

#define dbglog_error( ... ) \
   if( NULL != g_dbglog_file ) { \
      pthread_mutex_lock( &g_dbglog_lock ); \
      fprintf( g_dbglog_file, \
         _dbglog_fmt( E, __FILE__, __LINE__ ) __VA_ARGS__ ); \
      fflush( g_dbglog_file ); \
      pthread_mutex_unlock( &g_dbglog_lock ); \
   }

#ifdef DBGLOG_C

int g_dbglog_level = 9;

FILE* g_dbglog_file = NULL;

pthread_mutex_t g_dbglog_lock;

static int dbglog_init( char* dbglog_path ) {
   int retval = 0;

   g_dbglog_file = fopen( dbglog_path, "w" );
   if( NULL == g_dbglog_file ) {
      retval = RETVAL_FILE;
   }

   return retval;
}

static void dbglog_shutdown() {
   fclose( g_dbglog_file );
}

#else

extern int g_dbglog_level;

extern FILE* g_dbglog_file;

extern pthread_mutex_t g_dbglog_lock;

#endif /* DBGLOG_C */

#endif /* !DBGLOG_H */



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "scangui.h"
#include "freediag_aif.h"

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

int checkReturn ()
{
  while ( 1 )
  {
    int c = getc ( stdin ) ;

    if ( c == -1 )
    {
      fprintf ( stderr, "scangui: Scantool seems to have crashed!?\n" ) ;
      return FALSE ;
    }

    if ( c == FREEDIAG_AIF_ERROR_RETURN )
    {
      fprintf ( stderr, "scangui: Operation failed.\n" ) ;
      return FALSE ;
    }

    if ( c == FREEDIAG_AIF_OK_RETURN )
    {
      fprintf ( stderr, "scangui: OK.\n" ) ;
      return TRUE ;
    }

    fprintf ( stderr, "scangui: Unexpected return from scantool (0x%02x)", c ) ;
  }
}


static int port_number = 0 ;
static int units       = 0 ;


void unitsChoiceCB ( Fl_Choice *me, void * )
{
  units = me -> value () ;

  putc ( FREEDIAG_AIF_SET      , stdout ) ;
  putc ( FREEDIAG_AIF_SET_UNITS, stdout ) ;
  putc ( units ? FREEDIAG_AIF_SET_UNITS_US :
                 FREEDIAG_AIF_SET_UNITS_METRIC, stdout ) ;
  fflush ( stdout ) ;

  if ( ! checkReturn () )
  {
    fprintf ( stderr, "Units change command failed?!?\n" ) ;
    return ;
  }
}


void deviceChoiceCB ( Fl_Choice *me, void * )
{
  port_number = me -> value () ;
}


void exitCB ( Fl_Button *me, void * )
{
  fprintf ( stderr, "Telling scantool to exit...\n" ) ;
  putc ( FREEDIAG_AIF_EXIT, stdout ) ;
  fflush ( stdout ) ;
  sleep ( 1 ) ;

  // Shouldn't be needed - except during development.

  system ( "killall scantool" ) ;

  fprintf ( stderr, "Exiting scangui.\n" ) ;
  exit ( 0 ) ;
}


void disconnectCar ()
{
  putc ( FREEDIAG_AIF_DISCONNECT, stdout ) ;
  fflush ( stdout ) ;
  checkReturn () ;
}


void enableDebugCB ( Fl_Light_Button *me, void * )
{
  int debug = (me -> value () != 0) ;

  putc ( FREEDIAG_AIF_DEBUG, stdout ) ;
  putc ( debug             , stdout ) ;
  fflush ( stdout ) ;

  if ( ! checkReturn () )
  {
    fprintf ( stderr, "Debug command failed?!?\n" ) ;
    return ;
  }
}


void connectToCarButtonCB ( Fl_Light_Button *me, void * )
{
  if ( me -> value () == 0 )  /* Disconnect */
  {
    disconnectCar () ;
    return ;
  }

  putc ( FREEDIAG_AIF_SET     , stdout ) ;
  putc ( FREEDIAG_AIF_SET_PORT, stdout ) ;
  putc ( port_number          , stdout ) ;
  fflush ( stdout ) ;

  if ( ! checkReturn () )
  {
    me -> value ( 0 ) ;  /* Nope - not connected */
    return ;
  }

  putc ( FREEDIAG_AIF_SCAN, stdout ) ;
  fflush ( stdout ) ;

  if ( ! checkReturn () )
  {
    me -> value ( 0 ) ;  /* Nope - not connected */
    return ;
  }

  me -> value ( 1 ) ;  /* Yay! Connected! */
}



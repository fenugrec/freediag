
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "scangui.h"
#include "freediag_aif.h"

void initFreediag ( void )
{
  int outFiledes [ 2 ] ;
  int  inFiledes [ 2 ] ;

  if ( pipe ( outFiledes ) != 0 || pipe ( inFiledes ) != 0 )
  {
    perror ( "scangui" ) ;  
    exit ( 1 ) ;
  }

  dup2 (  inFiledes[0], fileno ( stdin  ) ) ;
  dup2 ( outFiledes[1], fileno ( stdout ) ) ;

  if ( fork () )
    return ;

  dup2 ( outFiledes[0], fileno ( stdin  ) ) ;
  dup2 (  inFiledes[1], fileno ( stdout ) ) ;

  execl ( "../scantool/scantool", "scantool", "-a", NULL ) ;
  perror ( "scangui" ) ;
  exit ( 1 ) ;
}



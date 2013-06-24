#include <string.h>
#include "vncPasswd.h"


int main( int argc , char *argv[] )
{
    if ( argc == 2 )
    {
        char passwd[MAXPWLEN+1];
        strncpy( passwd , argv[ 1 ] , MAXPWLEN );
        vncPasswd::FromText crypt(passwd);
        if ( PrfWriteProfileData(HINI_USERPROFILE, "ER_PMVNCD", "Password", (PVOID)((const char *)crypt), MAXPWLEN) )
        {
            printf( "done.\n" );
            return 0;
        }
        else
        {
            printf( "Error, password not set.\n" );
            return 1;
        }
    }

    printf( "Usage: vncpass <password>\n" );
    return 1;
}

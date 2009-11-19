#include <stdio.h>
#include <string.h>
#include <stdlib.h>

%%{
    machine inapt;

    action pkgstart {
      ts = p;
    }

    action pkgend {
      te = p;

      char *q;
      printf("package: ");
      for (q = ts; q <= te; q++) {
        putchar(*q);
      }
      printf("\n");
    }

    package = ((lower | digit) (lower | digit | '+' | '-' | '.')+) >pkgstart %pkgend;
    main := (package space+)*;
}%%

%% write data nofinal;

#define BUFSIZE 128

void scanner()
{
    static char buf[BUFSIZE];
    int cs, have = 0;
    int done = 0;
    char *ts = 0, *te = 0;

    %% write init;

    while ( !done ) {
        char *p = buf + have, *pe, *eof = 0;
        int len, space = BUFSIZE - have;

        if ( space == 0 ) {
            /* We've used up the entire buffer storing an already-parsed token
             * prefix that must be preserved. */
            fprintf(stderr, "OUT OF BUFFER SPACE\n" );
            exit(1);
        }

        len = fread( p, 1, space, stdin );
        pe = p + len;

        /* Check if this is the end of file. */
        if ( len < space ) {
            eof = pe;
            done = 1;
        }

        %% write exec;

        if ( cs == inapt_error ) {
            fprintf(stderr, "PARSE ERROR\n" );
            break;
        }

        have = 0;

        if (ts) {
            have = pe -ts;
            memmove(buf, ts, have);
            te = buf + (te - ts);
            ts = buf;
        }
    }
}

int main()
{
    scanner();
    return 0;
}

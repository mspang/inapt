#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

#include "inapt.h"
#include "util.h"

using namespace std;

#define xstrndup strndup

%%{
    machine inapt;

    action pkgstart { ts = p; }

    action add_list {
        inapt_action *tmp_action = new inapt_action;
        tmp_action->package = xstrndup(ts, p - ts);
        tmp_action->action = curaction;
        tmp_action->linenum = curline;
        tmp_action->filename = curfile;
        cur_context->actions.push_back(tmp_action);
    }

    action install {
        curaction = inapt_action::INSTALL;
    }

    action remove {
        curaction = inapt_action::REMOVE;
    }

    action newline {
        curline += 1;
    }

    action misc_error {
        fprintf(stderr, "%s: %d: Syntax Error\n", curfile, curline);
    }

    newline = '\n' %newline;
    comment = '#' (any - newline)* newline;
    whitespace = [\t\v\f\r ] | comment | newline;
    package_name = ((lower | digit) (lower | digit | '+' | '-' | '.')+) >pkgstart;
    package_list = ((whitespace+ package_name)+ %add_list whitespace*);
    cmd_install = ('install' @install package_list ';');
    cmd_remove = ('remove' @remove package_list ';');
    simple_cmd = cmd_install | cmd_remove;
    cmd_if = 'if' whitespace+ alpha+ whitespace+ simple_cmd whitespace* ('else' whitespace+ simple_cmd)?;
    statement = simple_cmd | cmd_if;
    cmd_list = (statement | whitespace)* ; #$err(misc_error);
    main := cmd_list;
}%%

%% write data;

#define BUFSIZE 128

void parser(const char *filename, inapt_context *top_context)
{
    static char buf[BUFSIZE];
    int fd;
    int cs, have = 0;
    int done = 0;
    int curline = 1;
    char *ts = 0, *te = 0;
    inapt_context *cur_context = top_context;
    //int stack[100];
    //int top = 0; /* TODO: resize */

    const char *curfile = filename;
    enum inapt_action::action_t curaction = inapt_action::UNSET;

    if (filename) {
        fd = open(filename, O_RDONLY);
        if (fd < 0)
            fatalpe("open: %s", filename);
    } else {
        curfile = "stdin";
        fd = 0;
    }

    %% write init;

    while ( !done ) {
        char *p = buf + have, *pe, *eof = 0;
        int len, space = BUFSIZE - have;

        if (space == 0) {
            fprintf(stderr, "OUT OF BUFFER SPACE\n");
            exit(1);
        }

        len = read(fd, p, space);
        if (len < 0) {
            fprintf(stderr, "IO ERROR\n");
            exit(1);
        }
        pe = p + len;

        if (!len) {
            eof = pe;
            done = 1;
        }

        %% write exec;

        if (cs == inapt_error) {
            fprintf(stderr, "PARSE ERROR\n");
	    exit(1);
        }

        have = 0;

        if (ts) {
            have = pe - ts;
            memmove(buf, ts, have);
            te = buf + (te - ts);
            ts = buf;
        }
    }

    if (cs < inapt_first_final) {
       fprintf(stderr, "UNEXPECTED EOF\n");
       exit(1);
    }
}

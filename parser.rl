#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <vector>

#include "inapt.h"
#include "util.h"

using namespace std;

#define MAXDEPTH 100
#define BUFSIZE 128

%%{
    machine inapt;

    action pkgstart { ts = p; }

    action add_list {
        inapt_action *tmp_action = new inapt_action;
        tmp_action->package = xstrndup(ts, p - ts);
        tmp_action->action = curaction;
        tmp_action->linenum = curline;
        tmp_action->filename = curfile;
        block_stack.back()->actions.push_back(tmp_action);
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

    action start_block {
        if (top < MAXDEPTH) {
            inapt_block *tmp_block = new inapt_block;
            block_stack.push_back(tmp_block);
            fcall main;
        } else {
            fatal("%s: %d: Syntax Error: Nesting Too Deep at '}'", curfile, curline);
        }
    }

    action end_block {
        if (top) {
            fret;
        } else {
            fatal("%s: %d: Syntax Error: Unexpected '}'", curfile, curline);
        }
    }

    action start_conditional {
        inapt_conditional *cond = new inapt_conditional;
        cond->condition = xstrndup(ts, p - ts);
        conditional_stack.push_back(cond);
    }

    action full_conditional {
        inapt_conditional *cond = conditional_stack.back(); conditional_stack.pop_back();
        cond->else_block = block_stack.back(); block_stack.pop_back();
        cond->then_block = block_stack.back(); block_stack.pop_back();
        block_stack.back()->children.push_back(cond);
    }

    action half_conditional {
        inapt_conditional *cond = conditional_stack.back(); conditional_stack.pop_back();
        cond->else_block = NULL;
        cond->then_block = block_stack.back(); block_stack.pop_back();
        block_stack.back()->children.push_back(cond);
    }

    newline = '\n' @newline;
    comment = '#' (any - '\n')* newline;
    whitespace = [\t\v\f\r ] | comment | newline;
    package_name = ((lower | digit) (lower | digit | '+' | '-' | '.')+) >pkgstart;
    package_list = ((whitespace+ package_name)+ %add_list whitespace*);
    cmd_install = ('install' @install package_list ';');
    cmd_remove = ('remove' @remove package_list ';');
    simple_cmd = cmd_install | cmd_remove;
    start_block = '{' @start_block;
    end_block = '}' @end_block;
    macro = alpha (alpha | digit | '-' | '+' | '.')+;
    cmd_if = 'if' whitespace+ macro >pkgstart %start_conditional whitespace* start_block whitespace*
             ('else' whitespace* start_block whitespace* ';' @full_conditional | ';' @half_conditional);
    cmd_list = (simple_cmd | cmd_if | whitespace)* end_block?;
    main := cmd_list;
}%%

%% write data;

void badsyntax(const char *filename, int lineno, char badchar, const char *message) {
    if (!message) {
        if (badchar == '\n')
            message = "Unexpected newline";
        else if (isspace(badchar))
            message = "Unexpected whitespace";
        else
            message = "Syntax error";
    }

    if (isprint(badchar) && !isspace(badchar))
        fatal("%s: %d: %s at '%c'", filename, lineno, message, badchar);
    else
        fatal("%s: %d: %s", filename, lineno, message);
}

void parser(const char *filename, inapt_block *top_block)
{
    static char buf[BUFSIZE];
    int fd;
    int cs, have = 0;
    int done = 0;
    int curline = 1;
    char *ts = 0;

    std::vector<inapt_block *> block_stack;
    std::vector<inapt_conditional *> conditional_stack;
    block_stack.push_back(top_block);

    int stack[MAXDEPTH];
    int top = 0;

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

    while (!done) {
        char *p = buf + have, *pe, *eof = 0;
        int len, space = BUFSIZE - have;

        if (!space)
            badsyntax(curfile, curline, 0, "Overlength token");

        len = read(fd, p, space);
        if (len < 0)
            fatalpe("Unable to read spec");
        pe = p + len;

        if (!len) {
            eof = pe;
            done = 1;
        }

        %% write exec;

        if (cs == inapt_error)
            badsyntax(curfile, curline, *p, NULL);

        have = 0;

        if (ts) {
            have = pe - ts;
            memmove(buf, ts, have);
            ts = buf;
        }
    }

    if (cs < inapt_first_final)
        badsyntax(curfile, curline, 0, "Unexpected EOF");

    if (top)
        badsyntax(curfile, curline, 0, "Unclosed block at EOF");
}

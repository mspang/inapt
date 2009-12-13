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

    action strstart { ts = p; }

    action add_alternate {
        std::string tmp (ts, p - ts); ts = 0;
        alternates.push_back(tmp);
    }

    action add_package {
        inapt_package *tmp_package = new inapt_package;
        tmp_package->alternates.swap(alternates);
        tmp_package->action = tmp_action->action;
        tmp_package->linenum = curline;
        tmp_package->filename = curfile;
        tmp_package->predicates.swap(pkg_predicates);
        tmp_action->packages.push_back(tmp_package);
    }

    action start_install {
        tmp_action = new inapt_action;
        tmp_action->action = inapt_action::INSTALL;
        tmp_action->predicates.swap(cmd_predicates);
        block_stack.back()->actions.push_back(tmp_action);
    }

    action start_remove {
        tmp_action = new inapt_action;
        tmp_action->action = inapt_action::REMOVE;
        tmp_action->predicates.swap(cmd_predicates);
        block_stack.back()->actions.push_back(tmp_action);
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
        cond->condition = xstrndup(ts, p - ts); ts = 0;
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

    action pkg_predicate {
        std::string tmp (ts, p - ts); ts = 0;
        pkg_predicates.push_back(tmp);
    }

    action cmd_predicate {
        std::string tmp (ts, p - ts); ts = 0;
        cmd_predicates.push_back(tmp);
    }

    newline = '\n' @newline;
    comment = '#' (any - '\n')* newline;
    whitespace = [\t\v\f\r ] | comment | newline;
    profile = '!'? alpha (alpha | digit | '-' | '+' | '.')*;
    package_name = ((lower | digit) (lower | digit | '+' | '-' | '.')+) >strstart;
    pkg_predicate = '@' profile >strstart %pkg_predicate whitespace+;
    cmd_predicate = '@' profile >strstart %cmd_predicate whitespace+;
    package_alternates = package_name >strstart %add_alternate ('/' package_name >strstart %add_alternate)*;
    package_list = ((whitespace+ pkg_predicate? package_alternates)+ %add_package whitespace*);
    cmd_install = ('install' @start_install package_list ';');
    cmd_remove = ('remove' @start_remove package_list ';');
    start_block = '{' @start_block;
    end_block = '}' @end_block;
    cmd_if = 'if' whitespace+ profile >strstart %start_conditional whitespace* start_block whitespace*
             ('else' whitespace* start_block whitespace* ';' @full_conditional | ';' @half_conditional);
    cmd = whitespace* (cmd_predicate? (cmd_install | cmd_remove) | cmd_if);
    cmd_list = cmd* whitespace* end_block?;
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
    std::vector<std::string> alternates;
    std::vector<std::string> cmd_predicates;
    std::vector<std::string> pkg_predicates;
    block_stack.push_back(top_block);
    inapt_action *tmp_action = NULL;

    int stack[MAXDEPTH];
    int top = 0;

    const char *curfile = filename;

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

/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of svpview. svpview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/*
 * sv_help.h — the manual, as data
 *
 * One copy of the text, in a table, rendered two ways: the Help page draws it
 * in the program, and `svpview --help-doc` writes it out as Markdown for
 * docs/HELP.md. A help file that is edited separately from the help page goes
 * stale the first time a control is renamed, so there is only the one.
 *
 * No SDL and no Nuklear here: this is content, and the tests link it.
 */
#ifndef SV_HELP_H
#define SV_HELP_H

#include <stdio.h>

typedef enum {
    SV_HELP_TEXT = 0,   /* a paragraph                                    */
    SV_HELP_SUB,        /* a sub-heading within the section               */
    SV_HELP_BULLET,     /* one bullet                                     */
    SV_HELP_ROW,        /* control or key on the left, what it does right */
    SV_HELP_NOTE        /* something that will cost the operator a cast   */
} SvHelpKind;

typedef struct {
    SvHelpKind  kind;
    const char *a;      /* paragraph, bullet, heading, or the left column */
    const char *b;      /* SV_HELP_ROW only: the right column             */
} SvHelpItem;

typedef struct {
    const char        *title;
    const char        *intro;   /* may be NULL */
    const SvHelpItem  *items;
    int                n_items;
} SvHelpSection;

/* The manual, in order. */
const SvHelpSection *sv_help_sections(int *n_sections);

/* Write the whole thing as Markdown — what `--help-doc` and the Makefile's
 * `help-doc` target use to regenerate docs/HELP.md. */
void sv_help_write_markdown(FILE *f, const char *version);

#endif /* SV_HELP_H */

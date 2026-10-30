/*
 * Copyright 2011 Daniel Silverstone <dsilvers@digital-scurf.org>
 *
 * This file is part of NetSurfy, http://www.netsurf-browser.org/
 *
 * NetSurfy is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurfy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETSURFY_MONKEY_DISPATCH_H
#define NETSURFY_MONKEY_DISPATCH_H 1

typedef void (*handle_command_fn)(int argc, char **argv);

nserror monkey_register_handler(const char *cmd, handle_command_fn fn);

void monkey_process_command(void);

void monkey_free_handlers(void);

#endif /* NETSURFY_MONKEY_DISPATCH_H */

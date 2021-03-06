/*
 Copyright (C) 2013-2014 Masakazu Ohtsuka
  
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
  
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
  
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __PGMSTRTORAM_H__
#define __PGMSTRTORAM_H__

#include <avr/pgmspace.h>

#define P(s)       pgmStrToRAM(PSTR(s),0)
#define PB(s,i)    pgmStrToRAM(PSTR(s),i)

#ifdef __cplusplus
extern "C" {
#endif

char *pgmStrToRAM(const PROGMEM char *theString, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif

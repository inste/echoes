//      wavfile.c
//      
//      Copyright 2010 Ilya <ilya@laptop>
//      
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//      
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//      
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//      MA 02110-1301, USA.


#include "wavfile.h"

void	read_sample_into_buffer(FILE * fd, char * buffer, int sample_size) {
	int i;

	for (i = 0; i < sample_size; ++i) {
		fread((char *)(buffer + i * 4), sizeof(char), 2, fd);
		fread((char *)(buffer + i * 4 + 2), sizeof(char), 2, fd);		
	}
}

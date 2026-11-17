#ifndef NETSURFY_UTILS_GETLINE_H
#define NETSURFY_UTILS_GETLINE_H

#include <stdio.h>
#include <sys/types.h>

#ifdef _WIN32
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

static inline ssize_t getline(char **line, size_t *capacity, FILE *stream)
{
	size_t length = 0;
	int ch;

	while ((ch = fgetc(stream)) != EOF) {
		if (*line == NULL || length + 1 >= *capacity) {
			size_t size = *line == NULL || *capacity < 128 ?
				      128 : *capacity * 2;
			char *buffer;

			if (size <= length + 1 || size > (size_t)PTRDIFF_MAX) {
				errno = EOVERFLOW;
				return -1;
			}
			buffer = realloc(*line, size);
			if (buffer == NULL) {
				errno = ENOMEM;
				return -1;
			}
			*line = buffer;
			*capacity = size;
		}
		(*line)[length++] = (char)ch;
		if (ch == '\n') {
			break;
		}
	}
	if (length == 0 || ferror(stream)) {
		return -1;
	}
	(*line)[length] = '\0';
	return (ssize_t)length;
}
#endif

#endif

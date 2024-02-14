/*
MIT License

Copyright (c) 2023-24 Ivan Nardi <nardi.ivan@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "pl7m.h"


int main(int argc, char *argv[])
{
	FILE *f_in, *f_out;
	size_t max_size = 1 * 1024 * 1024; /* We don't have a real limit */
	unsigned int seed;
	char *out_file;
	char *endptr;
	char buf[1024];

	if (argc < 2) {
		fprintf(stderr, "Usage: %s input file [output file] [seed]\n",
			argv[0]);
		return -1;
	}

	f_in = fopen(argv[1], "r");
	if (!f_in) {
		fprintf(stderr, "Error fopen [%s]: %d\n", argv[1], errno);
		return -1;
	}
	if (argc == 3 || argc == 4) {
		out_file = argv[2];
	} else {
		snprintf(buf, sizeof(buf), "%s.fuzzed", argv[1]);
		out_file = buf;
	}
	if (argc == 4) {
		seed = strtol(argv[3], &endptr, 0);
	} else {
		seed = 0xABCDABCE;
	}

	f_out = fopen(out_file, "w");
	if (!f_out) {
		fprintf(stderr, "Error fopen [%s]: %d\n", out_file, errno);
		fclose(f_in);
		return -1;
	}

	pl7m_mutator_fd(f_in, f_out, max_size, seed);

	return 0;
}

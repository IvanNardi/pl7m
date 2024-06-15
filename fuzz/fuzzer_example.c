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

/*
 * A simple fuzzer to show how to integrate the pl7m mutator into you own fuzzer
 */

#include <stdlib.h>
#include <string.h>


/* [1] Include the pl7m header */
#include "pl7m.h"



/* [2] That is the key point: add a new custom mutator calling pl7m mutator */
#ifdef __cplusplus
extern "C"
#endif
size_t LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size,
                               size_t MaxSize, unsigned int Seed)
{
	return pl7m_mutator(Data, Size, MaxSize, Seed);
}

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	/* [3] Your fuzzer real logic will be here */

	/* As an example, we simply try to fuzz the mutator logic! */

	uint8_t *buf;
	size_t max_size = (Size >= 24) ? Size * 2 : 24;
	buf = malloc(max_size);
	memcpy(buf, Data, Size);
	pl7m_mutator(buf, Size, max_size, 0xabcdef01);
	free(buf);

	return 0;
}

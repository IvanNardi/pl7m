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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <pcap.h>

#include "pl7m.h"

#if defined(__has_feature)
# if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#endif
#endif

#ifdef __cplusplus
extern "C"
#endif

/*	Configuration options/defines:
	* PL7M_ENABLE_ERROR: to enable logging of important/critical errors
	* PL7M_ENABLE_LOG: to enable verbose logging
	* PL7M_USE_INTERNAL_FUZZER_MUTATE: instead of using the standard function
	  `LLVMFuzzerMutate()` provided by libfuzz, use a custom/internal logic
	  to randomize the data. It is usefull if you want to use this code
	  without linking to libfuzz
	* PL7M_USE_SIMPLEST_MUTATOR: instead of fuzzing only the L7 part of the
	  packets, randomize the entire data. Note that the output of the
	  mutator will be a valid pcap file anyway
	* PL7M_DISABLE_PACKET_MUTATION: disable mutations at packet level (see
	  below). The output trace contains the same packets (in the same order
	  and with the same timestamp) of the input trace
	* PL7M_DISABLE_PAYLOAD_MUTATION: disable mutations at payload level
	  (see below)

	Mutations happens at two different levels:
	* packet level: each packet might be dropped, duplicated, swapped or
	   its direction might be swapped
	* payload level: packet (L5/7) payload (i.e. data after TCP/UDP header)
	  is changed
*/



#ifndef IPPROTO_IPV4
#define IPPROTO_IPV4	4
#endif
#ifndef IPPROTO_OSPF
#define IPPROTO_OSPF	89
#endif
#ifndef IPPROTO_VRRP
#define IPPROTO_VRRP	112
#endif
#ifndef IPPROTO_PGM
#define IPPROTO_PGM	113
#endif

#ifdef PL7M_ENABLE_ERROR
#define derr(fmt, args...) \
	do { \
		fprintf(stderr, "" fmt, ## args);\
	} while (0)
#else
#define derr(fmt, args...) \
	do { \
	} while (0)
#endif

#ifdef PL7M_ENABLE_LOG
#define ddbg(fmt, args...) \
	do { \
		fprintf(stderr, "" fmt, ## args);\
	} while (0)
#else
#define ddbg(fmt, args...) \
	do { \
	} while (0)
#endif


#define MAX_PKT_LENGTH	1024 * 32	/* It is so big to handle TSO packets */

#ifndef PL7M_USE_INTERNAL_FUZZER_MUTATE
size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);
#endif

struct m_pkt {
	unsigned char *raw_data;
	struct pcap_pkthdr header;

	int l2_offset;
	int prev_l3_offset;
	u_int16_t prev_l3_proto;
	int l3_offset;
	u_int16_t l3_proto;
	int l4_offset;
	u_int8_t l4_proto;
	int l5_offset;
	int l5_length;

	int is_l3_fragment;
	int skip_l4_dissection;
	int skip_payload_actions;

	struct m_pkt *next;
};
struct pl7m_handle {
	int datalink;

	struct m_pkt *head;
	struct m_pkt *tail;
};


/*
	Dissection code: START
*/

static int __is_datalink_supported(int datalink_type)
{
	switch(datalink_type) {
	case DLT_NULL:
	case DLT_EN10MB:
	case DLT_PPP:
	case DLT_C_HDLC:
	case DLT_RAW:
	case DLT_LINUX_SLL:
	case DLT_LINUX_SLL2:
	case DLT_IPV4:
	case DLT_IPV6:
	case DLT_PPI:
		return 1;
	default:
	return 0;
	}
}
static int __is_l3_proto_supported(int proto)
{
	switch(proto) {
	case ETH_P_IP:
	case ETH_P_IPV6:
	case ETH_P_ARP:
		/* TODO: add other protocols */
		return 1;
	default:
		return 0;
	}
}
static int dissect_l2(int datalink_type, struct m_pkt *p)
{
	int l2_offset, l3_offset;
	u_int16_t l3_proto = 0, next, header_length;
	u_int32_t dlt;
	unsigned char *data = p->raw_data;
	int data_len = p->header.caplen;

	if (data_len <= 0) {
		derr("Invalid len %d\n", data_len);
		return -1;
	}

	l2_offset = p->l2_offset;
	assert(l2_offset >= 0 && l2_offset < (int)p->header.caplen);

	switch(datalink_type) {
	case DLT_NULL:
		if (data_len < l2_offset + 5)
			return -1;
		l3_offset = l2_offset + 4;
		if ((data[l3_offset] & 0xF0) == 0x40)
			l3_proto = ETH_P_IP;
		else if ((data[l3_offset] & 0xF0) == 0x60)
			l3_proto = ETH_P_IPV6;
		break;

	case DLT_RAW:
		if (data_len < l2_offset + 1)
			return -1;
		l3_offset = l2_offset + 0;
		if ((data[l3_offset] & 0xF0) == 0x40)
			l3_proto = ETH_P_IP;
		else if ((data[l3_offset] & 0xF0) == 0x60)
			l3_proto = ETH_P_IPV6;
		break;

	case DLT_IPV4:
		l3_proto = ETH_P_IP;
		l3_offset = l2_offset + 0;
		break;

	case DLT_IPV6:
		l3_proto = ETH_P_IPV6;
		l3_offset = l2_offset + 0;
		break;

	case DLT_LINUX_SLL:
		if (data_len < l2_offset + 16)
			return -1;
		l3_proto = ntohs(*((u_int16_t *)&data[l2_offset + 14]));
		l3_offset = 16;
		break;

	case DLT_LINUX_SLL2:
		if (data_len < l2_offset + 20)
			return -1;
		l3_proto = ntohs(*((u_int16_t *)&data[l2_offset]));
		l3_offset = 20;
		break;

	case DLT_PPI:
		if (data_len < l2_offset + 4)
			return -1;
		header_length = le16toh(*(u_int16_t *)&data[l2_offset + 2]);
		dlt = le32toh(*(u_int32_t *)&data[l2_offset + 4]);
		if(dlt != DLT_EN10MB) /* Handle only standard ethernet, for the time being */
			return -1;
		p->l2_offset += header_length;
		if (p->l2_offset >= (int)p->header.caplen)
			return -1;
		return dissect_l2(dlt, p);

	case DLT_PPP:
	case DLT_C_HDLC:
		if (data[l2_offset + 0] == 0x0f || data[l2_offset + 0] == 0x8f) {
			l3_offset = 4;
			l3_proto = ntohs(*((u_int16_t *)&data[l2_offset + 2]));
		} else {
			l3_offset = l2_offset + 2;
			next = ntohs(*((u_int16_t *)&data[l2_offset + 0]));
			switch (next) {
			case 0x0021:
				l3_proto = ETH_P_IP;
				break;
			case 0x0057:
				l3_proto = ETH_P_IPV6;
				break;
			default:
				derr("Unknown next proto on ppp 0x%x\n", next);
				return -1;
			}
		}
		break;

	case DLT_EN10MB:
		if (data_len < l2_offset + 14)
			return -1;
		l3_offset = l2_offset + 14;
		l3_proto = ntohs(*((u_int16_t *)&data[l3_offset - 2]));

		/* VLAN */
		while (l3_proto == 0x8100 && l3_offset + 4 < data_len) {
			l3_offset += 4;
			l3_proto = ntohs(*((u_int16_t *)&data[l3_offset - 2]));
		}

		/* PPPoES */
		if (l3_proto == 0x8864) {
			if (data_len < l3_offset + 8)
				return -1;
			l3_offset += 8;
			next = ntohs(*((u_int16_t *)&data[l3_offset - 2]));
			switch (next) {
			case 0x0021:
				l3_proto = ETH_P_IP;
				break;
			case 0x0057:
				l3_proto = ETH_P_IPV6;
				break;
			default:
				derr("Unknown next proto on pppoes 0x%x\n", next);
				return -1;
			}
		}

		break;

	default:
		derr("Unknown datalink %d\n", datalink_type);
		return -1;
	}

	if (data_len < l3_offset) {
		derr("Invalid length %d < %d\n", data_len, l3_offset);
		return -1;
	}
	if (!__is_l3_proto_supported(l3_proto)) {
		derr("Unsupported l3_proto 0x%x\n", l3_proto);
		return -1;
	}
	p->l3_offset = l3_offset;
	p->l3_proto = l3_proto;

	return 0;
}
static int __is_l4_proto_supported(int proto)
{
	switch(proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
	case IPPROTO_IGMP:
	case IPPROTO_VRRP:
	case IPPROTO_AH:
	case IPPROTO_ESP:
	case IPPROTO_SCTP:
	case IPPROTO_PGM:
	case IPPROTO_PIM:
	case IPPROTO_IPV4:
	case IPPROTO_IPV6:
	case IPPROTO_GRE:
	case IPPROTO_OSPF:
		/* TODO: add other protocols */
		return 1;
	default:
		return 0;
	}
}
static int dissect_l3(struct m_pkt *p)
{
	struct ip *ip4;
	struct ip6_hdr *ip6;
	struct ip6_ext *ipv6_opt;
	int num_eh, ip_hdr_len, l3_len;
	unsigned char *data = p->raw_data + p->l3_offset;
	int data_len = p->header.caplen - p->l3_offset;

	ddbg("L3: l3_proto %d data_len %d\n", p->l3_proto, data_len);

	if (data_len < 0)
		return -1;

	switch (p->l3_proto) {
	case ETH_P_IP:
		ip4 = (struct ip *)data;
		if (ip4->ip_v != 4 ||
		    data_len < 20 /* min */ ||
		    ip4->ip_hl < 5 ||
		    data_len < ip4->ip_hl * 4) {
			derr("Wrong lengths %d %d\n", data_len, ip4->ip_hl);
			return -1;
		}
		/* TODO: properly handle fragments */
		if ((ntohs(ip4->ip_off) & IP_MF) ||
		    (ntohs(ip4->ip_off) & IP_OFFMASK)) {
			ddbg("Fragment\n");
			p->is_l3_fragment = 1;
			p->skip_payload_actions = 1;
		}
		if (!__is_l4_proto_supported(ip4->ip_p)) {
			derr("Unsupported L4: %d\n", ip4->ip_p);
			return -1;
		}
		p->l4_proto = ip4->ip_p;
		p->l4_offset = p->l3_offset + ip4->ip_hl * 4;
		break;

	case ETH_P_IPV6:
		ip6 = (struct ip6_hdr *)data;
		if (data_len < (int)sizeof(struct ip6_hdr))
			return -1;

		/* It may be a IPv6 Jumbograms but it is probably a
		   malformed packet */
		if (ip6->ip6_plen == 0) {
			derr("Invalid ext len\n");
			return -1;
		}

		ip_hdr_len = sizeof(struct ip6_hdr);
		l3_len = ntohs(ip6->ip6_plen) + ip_hdr_len;
		if (l3_len < ip_hdr_len || data_len < l3_len) {
			derr("Invalid ipv6 lengths %d %d %d\n",
			     l3_len, ip_hdr_len, data_len);
			return -1;
		}

		p->l4_proto = ip6->ip6_nxt;

		/* Extension header */
		num_eh = 0;
		while (p->l4_proto == IPPROTO_HOPOPTS ||
		       p->l4_proto == IPPROTO_DSTOPTS ||
		       p->l4_proto == IPPROTO_ROUTING ||
		       p->l4_proto == IPPROTO_AH ||
		       p->l4_proto == IPPROTO_FRAGMENT) {

			num_eh++;

			if (data_len < ip_hdr_len + (int)sizeof(struct ip6_ext)) {
				derr("Error ipv6 (a) %d %d\n", data_len, ip_hdr_len);
				return -1;
			}
			if (ip_hdr_len >= l3_len) {
				derr("Error ipv6 (b) %d %d\n", ip_hdr_len, l3_len);
				return -1;
			}
			/* RFC2460 4.1 . Hop-by-Hop Options header [..] is
			   restricted to appear immediately after an IPv6 header only */
			if (p->l4_proto == IPPROTO_HOPOPTS && num_eh != 1) {
				derr("Hop-by-Hop Options not first header\n");
				return -1;
			}

			ipv6_opt = (struct ip6_ext *)&data[ip_hdr_len];
			ip_hdr_len += sizeof(struct ip6_ext);
			ddbg("EH (%d) %d ip6e_len %d\n", num_eh, p->l4_proto,
			     ipv6_opt->ip6e_len);
			if (p->l4_proto == IPPROTO_AH) {
				/* RFC4302 2.2. Payload Length: This 8-bit
				   field specifies the length of AH in
				   32-bit words (4-byte units), minus "2". */
				ip_hdr_len += ipv6_opt->ip6e_len * 4;
			} else if (p->l4_proto == IPPROTO_HOPOPTS) {
				/* RFC2460 4.3. Hdr Ext Len: Length of the Hop-by-Hop
				   Options header in 8-octet units, not including
				   the first 8 octets */
				ip_hdr_len += (6 + ipv6_opt->ip6e_len * 8);
			} else {
				if (p->l4_proto != IPPROTO_FRAGMENT) {
					ip_hdr_len += ipv6_opt->ip6e_len;
				} else {
					ip_hdr_len += 6;
					ddbg("Fragment IPv6\n");
					p->is_l3_fragment = 1;
					p->skip_payload_actions = 1;
				}
			}
			p->l4_proto = ipv6_opt->ip6e_nxt;

			if (ip_hdr_len >= l3_len) {
				derr("Error ipv6 (c) %d %d\n", ip_hdr_len, l3_len);
				return -1;
			}
		}

		if (!__is_l4_proto_supported(p->l4_proto)) {
			derr("Unsupported L4: %d\n", p->l4_proto);
			return -1;
		}
		p->l4_proto = ip6->ip6_nxt;
		p->l4_offset = p->l3_offset + ip_hdr_len;
		break;

	case ETH_P_ARP:
		p->skip_l4_dissection = 1;
		p->skip_payload_actions = 1;
		break;

	default:
		assert(0);
	}
	return 0;
}
static int dissect_l4(struct m_pkt *p)
{
	struct udphdr *udp_h;
	struct tcphdr *tcp_h;
	unsigned char *data = p->raw_data + p->l4_offset;
	int data_len = p->header.caplen - p->l4_offset;
	int l4_hdr_len, rc;

	ddbg("L4: l4_proto %d data_len %d\n", p->l4_proto, data_len);

	if (data_len < 0)
		return -1;

	if (p->is_l3_fragment) {
		ddbg("Skip L4 dissection because it is a fragment\n");
		return 0;
	}
	if (p->skip_l4_dissection) {
		ddbg("Skip L4 dissection\n");
		return 0;
	}

	switch(p->l4_proto) {
	case IPPROTO_UDP:
		udp_h = (struct udphdr *)data;
		if (data_len < (int)sizeof(struct udphdr) ||
		    ntohs(udp_h->len) > data_len ||
		    ntohs(udp_h->len) < sizeof(struct udphdr)) {
			derr("Unexpected udp len %u vs %u\n",
			     ntohs(udp_h->len), data_len);
			return -1;
		}
		p->l5_offset = p->l4_offset + sizeof(struct udphdr);
		p->l5_length = ntohs(udp_h->len) - sizeof(struct udphdr);
		break;
	case IPPROTO_TCP:
		tcp_h = (struct tcphdr *)data;
		if (data_len < (int)sizeof(struct tcphdr)) {
			derr("Unexpected tcp len %u\n", data_len);
			return -1;
		}
		l4_hdr_len = tcp_h->doff << 2;
		if (l4_hdr_len < (int)sizeof(struct tcphdr) ||
		    l4_hdr_len > data_len) {
			derr("Unexpected tcp len %u %u\n",
			     l4_hdr_len, data_len);
			return -1;
		}
		p->l5_offset = p->l4_offset + l4_hdr_len;
		p->l5_length = data_len - l4_hdr_len;
		break;

	case IPPROTO_IPV4: /* IP in IP tunnel */
	case IPPROTO_IPV6: /* IP in IP tunnel */
		if (p->prev_l3_proto == 0) {
			assert(p->prev_l3_offset == 0);
			p->prev_l3_proto = p->l3_proto;
			p->prev_l3_offset = p->l3_offset;
		} else {
			derr("More than 2 ip headers. Unsupported\n");
			return -1;
		}
		p->l3_offset = p->l4_offset;
		p->l3_proto = (p->l4_proto == IPPROTO_IPV4) ? ETH_P_IP : ETH_P_IPV6;
		rc = dissect_l3(p);
		if (rc != 0) {
			derr("Error dissect_l3 (second header)\n");
			return -1;
		}
		return dissect_l4(p);


	default:
		/* Fuzz also the L4 header itself, but leave at least 1 byte:
		   this way, we will never have an empty ip4/6 header */
		if (data_len == 0)
			return -1;
		p->l5_offset = p->l4_offset + 1;
		p->l5_length = data_len - 1;
		break;
	}
	return 0;
}
static int dissect_do(int datalink_type, struct m_pkt *p)
{
	int rc;

	rc = dissect_l2(datalink_type, p);
	if (rc != 0) {
		derr("Error dissect_l2\n");
		return -1;
	}
	rc = dissect_l3(p);
	if (rc != 0) {
		derr("Error dissect_l3\n");
		return -1;
	}
	rc = dissect_l4(p);
	if (rc != 0) {
		derr("Error dissect_l4\n");
		return -1;
	}
	return 0;
}

/*
	Dissection code: END
*/


#ifdef PL7M_USE_INTERNAL_FUZZER_MUTATE
static size_t internal_FuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize)
{
	int r;
	unsigned char rand_byte;
	size_t new_len = Size, bytes_to_zero, offset;

	r = rand();
	switch (r % 8) {
	case 0: /* Unchange */
		ddbg("Payload action unchange\n");
		new_len = Size;
		break;
	case 1: /* Remove last bytes */
		ddbg("Payload action remove last bytes\n");
		if (Size > 0)
			new_len = rand() % Size;
		break;
	case 2: /* Remove first bytes */
		ddbg("Payload action remove first bytes\n");
		if (Size > 0) {
			new_len = rand() % Size;
			memmove(Data, Data + (Size - new_len), new_len);
		}
		break;
	case 3: /* Append zero bytes */
		ddbg("Payload action append zero bytes\n");
		new_len = rand() % MaxSize;
		if (new_len > Size)
			memset(&Data[Size], '\0', new_len - Size);
		else
			new_len = Size;
		break;
	case 4: /* Zero first bytes */
		ddbg("Payload action zero first bytes\n");
		if (Size > 0) {
			bytes_to_zero = rand() % Size;
			memset(Data, '\0', bytes_to_zero);
		}
		break;
	case 5: /* Zero last bytes */
		ddbg("Payload action zero last bytes\n");
		if (Size > 0) {
			bytes_to_zero = rand() % Size;
			memset(Data + (Size - bytes_to_zero), '\0', bytes_to_zero);
		}
		break;
	case 6:
		ddbg("Add a random byte at random location\n");
		if (MaxSize >= Size + 1) {
			offset = Size == 0 ? 0 : rand() % Size;
			rand_byte =  rand() % 255;
			new_len = Size + 1;
			memmove(Data + offset + 1, Data + offset, Size - offset);
			Data[offset] = rand_byte;
		}
		break;
	case 7:
		ddbg("Remove the byte from a random location\n");
		if (Size > 0) {
			offset = rand() % Size;
			new_len = Size - 1;
			memmove(Data + offset, Data + offset + 1, Size - offset - 1);
		}
		break;
	}
	return new_len;
}
#endif


static void update_do_l7(struct m_pkt *p)
{
	struct udphdr *udp_h;
	struct tcphdr *tcp_h;
	size_t new_l5_len;
	int l4_header_len = 0;
	int l5_len_diff;
	struct ip *ip4;
	struct ip6_hdr *ip6;

	assert(p->l5_offset + p->l5_length <= (int)p->header.caplen);
	assert(p->header.caplen <= MAX_PKT_LENGTH);
#ifndef PL7M_USE_INTERNAL_FUZZER_MUTATE
	new_l5_len = LLVMFuzzerMutate(p->raw_data + p->l5_offset,
				      p->l5_length, MAX_PKT_LENGTH - p->l5_offset);
	/* It seems the MASAN returns false positives. The value from
	   LLVMFuzzerMutate needs to be treated as initialized.
	   See a similar report:
	   https://github.com/google/libprotobuf-mutator/pull/213/commits/51629aaf874b38c42f5dc8b970cdf9156895c7e3
	*/
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
	__msan_unpoison(p->raw_data + p->l5_offset, new_l5_len);
# endif
#endif

#else
	new_l5_len = internal_FuzzerMutate(p->raw_data + p->l5_offset,
					   p->l5_length,
					   MAX_PKT_LENGTH - p->l5_offset);
#endif
	l5_len_diff = new_l5_len - p->l5_length;
	ddbg("l5_len %u->%zu (%d)\n", p->l5_length, new_l5_len, l5_len_diff);

	switch (p->l4_proto) {
	case IPPROTO_UDP:
		l4_header_len = sizeof(struct udphdr);
		udp_h = (struct udphdr *)(p->raw_data + p->l4_offset);
		udp_h->len = htons(l4_header_len + new_l5_len);
		break;
	case IPPROTO_TCP:
		tcp_h = (struct tcphdr *)(p->raw_data + p->l4_offset);
		l4_header_len = tcp_h->doff << 2;
		/* TODO */
		break;
	default:
		l4_header_len = 1;
		break;
	}

	if (p->l3_proto == ETH_P_IP) {
		ip4 = (struct ip *)(p->raw_data + p->l3_offset);
		ip4->ip_len = htons(ip4->ip_hl * 4 + l4_header_len + new_l5_len);
	} else {
		assert(p->l3_proto == ETH_P_IPV6);
		ip6 = (struct ip6_hdr *)(p->raw_data + p->l3_offset);
		/* We need to take into account existing extension headers */
		assert(p->l4_offset - p->l3_offset - (int)sizeof(struct ip6_hdr) >= 0);
		ip6->ip6_plen = htons((p->l4_offset - p->l3_offset - sizeof(struct ip6_hdr)) +
				      l4_header_len + new_l5_len);
	}

	/* Update previous ip header */
	if(p->prev_l3_proto == ETH_P_IP) {
		ip4 = (struct ip *)(p->raw_data + p->prev_l3_offset);
		ip4->ip_len = htons(htons(ip4->ip_len) + l5_len_diff);

	} else if(p->prev_l3_proto == ETH_P_IPV6) {
		ip6 = (struct ip6_hdr *)(p->raw_data + p->prev_l3_offset);
		ip6->ip6_plen = htons(ntohs(ip6->ip6_plen) + l5_len_diff);
	}

	p->l5_length = new_l5_len;
	ddbg("cap_len %u->%zu\n", p->header.caplen, p->l5_offset + new_l5_len);
	p->header.caplen = p->l5_offset + new_l5_len;
	p->header.len = p->header.caplen;
	assert(p->header.caplen <= MAX_PKT_LENGTH);
}

#ifdef PL7M_USE_SIMPLEST_MUTATOR
static void update_do_simplest(struct m_pkt *p)
{
	size_t new_len;

#ifndef PL7M_USE_INTERNAL_FUZZER_MUTATE
	new_len = LLVMFuzzerMutate(p->raw_data, p->header.caplen, MAX_PKT_LENGTH);
#else
	new_len = internal_FuzzerMutate(p->raw_data, p->header.caplen, MAX_PKT_LENGTH);
#endif

	p->header.caplen = new_len;
	p->header.len = p->header.caplen;
	assert(p->header.caplen <= MAX_PKT_LENGTH);
}
#endif

static void update_do(struct m_pkt *p)
{
#ifdef PL7M_USE_SIMPLEST_MUTATOR
	update_do_simplest(p);
#else
	update_do_l7(p);
#endif
}

static void swap_direction(struct m_pkt *p)
{
	struct udphdr *udp_h;
	u_int16_t tmp_port;

	/* Length fields don't change */

	if (p->skip_l4_dissection == 1)
		return;

	switch (p->l4_proto) {
	case IPPROTO_UDP:
		udp_h = (struct udphdr *)(p->raw_data + p->l4_offset);
		tmp_port = udp_h->source;
		udp_h->source = udp_h->source;
		udp_h->dest = tmp_port;
		break;
	case IPPROTO_TCP:
		/* TODO */
		break;
	default:
		/* Nothing to do */
		break;
	}

	if (p->l3_proto == ETH_P_IP) {
		struct ip *ip4;
		struct in_addr tmp_ip4;

		ip4 = (struct ip *)(p->raw_data + p->l3_offset);
		tmp_ip4 = ip4->ip_src;
		ip4->ip_src = ip4->ip_dst;
		ip4->ip_dst = tmp_ip4;
	} else {
		struct ip6_hdr *ip6;
		struct in6_addr tmp_ip6;

		assert(p->l3_proto == ETH_P_IPV6);
		ip6 = (struct ip6_hdr *)(p->raw_data + p->l3_offset);
		tmp_ip6 = ip6->ip6_src;
		ip6->ip6_src = ip6->ip6_dst;
		ip6->ip6_dst = tmp_ip6;
	}

	/* We probably don't need to swap direction of previous
	   ip header (if any) */
}

static struct m_pkt *__dup_pkt(struct m_pkt *p)
{
	struct m_pkt *n;

	n = (struct m_pkt *)malloc(sizeof(struct m_pkt));
	if (!n)
		return NULL;
	n->raw_data = (unsigned char *)malloc(MAX_PKT_LENGTH);
	if(!n->raw_data) {
		free(n);
		return NULL;
	}
	memcpy(n->raw_data, p->raw_data, p->header.caplen);
	n->header = p->header;
	n->l2_offset = p->l2_offset;
	n->prev_l3_offset = p->prev_l3_offset;
	n->prev_l3_proto = p->prev_l3_proto;
	n->l4_offset = p->l4_offset;
	n->l3_offset = p->l3_offset;
	n->l3_proto = p->l3_proto;
	n->l4_offset = p->l4_offset;
	n->l4_proto = p->l4_proto;
	n->l5_offset = p->l5_offset;
	n->l5_length = p->l5_length;
	n->is_l3_fragment = p->is_l3_fragment;
	n->skip_l4_dissection = p->skip_l4_dissection;
	n->skip_payload_actions = p->skip_payload_actions;
	n->next = NULL;
	return n;
}
static void __free_pkt(struct m_pkt *p)
{
	free(p->raw_data);
	free(p);
}
static void __add_pkt(struct pl7m_handle *h, struct m_pkt *p,
		      struct m_pkt *prev, struct m_pkt *next)
{
	if (prev) {
		prev->next = p;
	} else {
		h->head = p;
	}
	p->next = next;
	if(p->next == NULL)
		h->tail = p;
}
static void __del_pkt(struct pl7m_handle *h, struct m_pkt *p, struct m_pkt *prev)
{
	if (prev) {
		prev->next = p->next;
	} else {
		h->head = p->next;
	}
	__free_pkt(p);
}
static int __swap_pkt(struct pl7m_handle *h, struct m_pkt *p, struct m_pkt *prev,
		      struct m_pkt *next)
{
	struct timeval ts;

	if (!next)
		return 0;

	/* Swap timestamps too. If the original pcap is ordered (in time),
	   the mutated one will be, too */
	ts = next->header.ts;

	if (prev) {
		prev->next = next;
		next->header.ts = p->header.ts;
	} else {
		h->head = next;
	}
	p->next = next->next;
	next->next = p;
	p->header.ts = ts;

	/* TODO: update tail */

	return 1;
}

static void __free_m_pkts(struct pl7m_handle *h)
{
	struct m_pkt *p, *n;

	if (!h)
		return;
	p = h->head;
	while (p) {
		n = p->next;
		__free_pkt(p);
		p = n;
	}
	free(h);
}

static void do_pkt_actions(struct pl7m_handle *h)
{
	int r;
	struct m_pkt *p, *prev, *d, *tmp_prev;

	p = h->head;
	prev = NULL;
	while (p) {
		r = rand();
		switch (r % 5) {
		case 0: /* Unchange */
			ddbg("Action unchange\n");
			prev = p;
			break;
		case 1: /* Drop */
			ddbg("Action drop\n");
			__del_pkt(h, p, prev);
			break;
		case 2: /* Duplicate */
			ddbg("Action dup\n");
			d = __dup_pkt(p);
			__add_pkt(h, d, prev, p);
			p->skip_payload_actions = 1;
			d->skip_payload_actions = 1;
			prev = p;
			break;
		case 3: /* Swap */
			ddbg("Action swap\n");
			tmp_prev = p->next;
			if (__swap_pkt(h, p, prev, p->next))
				prev = tmp_prev;
			else
				prev = p;
			p->skip_payload_actions = 1;
			break;
		case 4: /* Swap direction */
			ddbg("Swap direction\n");
			swap_direction(p);
			prev = p;
			p->skip_payload_actions = 1;
			break;
		}
		if (prev)
			p = prev->next;
		else
			p = h->head;
	}
}

static void do_payload_actions(struct pl7m_handle *h)
{
	struct m_pkt *p;

	ddbg("Payload Actions\n");

	p = h->head;
	while (p) {
		if (!p->skip_payload_actions)
			update_do(p);
		p = p->next;
	}
}

static size_t __serialize_to_fd(struct pl7m_handle *h, FILE *fd_out,
				size_t max_data_len)
{
	pcap_t *pcap_h;
	pcap_dumper_t *pdumper;
	struct m_pkt *p;
	size_t written;

	/* We must be sure to not write more than max_data_len bytes.
	   PCAP file format:
	   * 24 bytes for global header
	   * packets (with a 16 bytes header)
	*/

	if (max_data_len < 24) {
		derr("Buffer too small: %zu vs 24\n", max_data_len);
		return 0;
	}

	pcap_h = pcap_open_dead(h->datalink, 65535 /* snaplen */);
	if (!pcap_h) {
		derr("Error pcap_open_dead\n");
		return 0;
	}

	pdumper = pcap_dump_fopen(pcap_h, fd_out);
	if (!pcap_h) {
		derr("Error pcap_dump_open");
		pcap_close(pcap_h);
		return 0;
	}
	written = 24;

	p = h->head;
	while (p) {
		if(written + 16 + p->header.caplen >= max_data_len) {
			ddbg("Buffer too small: %zu %u %zu. Skipping packet(s)\n",
			     written, p->header.caplen, max_data_len);
			break;
		}
		pcap_dump((u_char *)pdumper, &p->header, p->raw_data);
		written += 16 + p->header.caplen;
		p = p->next;
		ddbg("dumping pkt\n");
	}

	assert(written <= max_data_len);

	pcap_dump_close(pdumper);
	pcap_close(pcap_h);
	return written;
}

static size_t __serialize(struct pl7m_handle *h, const unsigned char *data,
			  size_t max_data_len)
{
	FILE *f_out;
	size_t data_len = max_data_len;

	f_out = fmemopen((void *)data, max_data_len, "w");
	if (!f_out) {
		derr("Error fmemopen\n");
		return 0;
	}

	data_len = __serialize_to_fd(h, f_out, max_data_len);
	return data_len;
}

static struct pl7m_handle *__deserialize_from_fd(FILE *fd_in)
{
	const u_char *data;
	struct pcap_pkthdr *header;
	struct pl7m_handle *h;
	struct m_pkt *p;
	pcap_t *pcap_h;
	char errbuf[PCAP_ERRBUF_SIZE];
	int rc;
#ifdef PL7M_ENABLE_LOG
	int pkt = 0;
#endif

	ddbg("Deserializing...\n");

	pcap_h = pcap_fopen_offline(fd_in, errbuf);
	if (pcap_h == NULL) {
		derr("Error pcap_open_offline: %s\n", errbuf);
		fclose(fd_in);
		return NULL;
	}

	if (__is_datalink_supported(pcap_datalink(pcap_h)) == 0) {
		derr("Datalink type %d not supported\n", pcap_datalink(pcap_h));
		pcap_close(pcap_h);
		return NULL;
	}

	h = (struct pl7m_handle *)calloc(1, sizeof(struct pl7m_handle));
	if (!h) {
		pcap_close(pcap_h);
		return NULL;
	}
	h->datalink = pcap_datalink(pcap_h);

	header = NULL;
	while (pcap_next_ex(pcap_h, &header, &data) > 0) {
		ddbg("Pkt %d\n", ++pkt);

		if (header->caplen > MAX_PKT_LENGTH) {
			derr("Pkt too big %i %i\n", header->caplen, MAX_PKT_LENGTH);
			/* Ignore current pkt, but keep going */
			continue;
		}
		p = (struct m_pkt *)calloc(sizeof(struct m_pkt), 1);
		if (!p) {
			__free_m_pkts(h);
			pcap_close(pcap_h);
			return NULL;
		}
		p->raw_data = (unsigned char *)malloc(MAX_PKT_LENGTH);
		if (!p->raw_data) {
			free(p);
			__free_m_pkts(h);
			pcap_close(pcap_h);
			return NULL;
		}
		assert(header->caplen <= MAX_PKT_LENGTH);
		memcpy(p->raw_data, data, header->caplen);
		p->header = *header;
		p->next = NULL;

		rc = dissect_do(h->datalink, p);
		if (rc != 0) {
			derr("Error dissect_do\n");
			/* Ignore current pkt, but keep going */
			free(p->raw_data);
			free(p);
			continue;
		}

		__add_pkt(h, p, h->tail, NULL);
		ddbg("Adding pkt (l5_len %d)\n", p->l5_length);
	}

	pcap_close(pcap_h);

	return h;
}

static struct pl7m_handle *__deserialize(const unsigned char *data,
					 size_t data_len)
{
	FILE *f_in;

	if (data_len == 0)
		return NULL;
	f_in = fmemopen((void *)data, data_len, "rw");
	if (!f_in) {
		derr("Error fmemopen\n");
		return NULL;
	}
	return __deserialize_from_fd(f_in);
}

static void __mutate(struct pl7m_handle *h, unsigned int seed)
{
	srand(seed);
#ifndef PL7M_DISABLE_PACKET_MUTATION
	do_pkt_actions(h);
#endif
#ifndef PL7M_DISABLE_PAYLOAD_MUTATION
	do_payload_actions(h);
#endif
}

/* Public functions */

size_t pl7m_mutator(uint8_t *data, size_t size, size_t max_size,
		    unsigned int seed)
{
	struct pl7m_handle *h;
	size_t new_size;
	static const uint8_t empty_pcap_file [24] = {
		0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };


	h = __deserialize(data, size);
	if (!h) {
		/* Return an empty, valid pcap file, if possible */
		if (max_size >= 24) {
			memcpy(data, empty_pcap_file, sizeof(empty_pcap_file));
			return sizeof(empty_pcap_file);
		}
		return 0;
	}
	__mutate(h, seed);
	new_size = __serialize(h, data, max_size);
	__free_m_pkts(h);
	return new_size;
}

size_t pl7m_mutator_fd(FILE *fd_in, FILE *fd_out, size_t max_size,
		       unsigned int seed)
{
	struct pl7m_handle *h;
	size_t new_size;
	static const uint8_t empty_pcap_file [24] = {
		0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };


	h = __deserialize_from_fd(fd_in);
	if (!h) {
		/* Return an empty, valid pcap file, if possible */
		if (max_size >= 24) {
			fwrite(empty_pcap_file, sizeof(empty_pcap_file), 1, fd_out);
			return sizeof(empty_pcap_file);
		}
		return 0;
	}
	__mutate(h, seed);
	new_size = __serialize_to_fd(h, fd_out, max_size);
	__free_m_pkts(h);
	return new_size;
}

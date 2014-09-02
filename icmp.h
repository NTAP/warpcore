#ifndef _icmp_h_
#define _icmp_h_

struct icmp_hdr {
	uint8_t		type;	// type of message
	uint8_t		code;	// type sub code
	uint16_t	cksum;	// ones complement checksum of struct
};


void icmp_receive(const char * const);

#endif

#include "resolving_network.h"

#include <stdio.h>
#include <stdlib.h>

ResolvingNetworkNode resolving_network_read_node(BitVector *network,
                                                  uint64_t node_index) {
    if (node_index > SIZE_MAX / 64) {
        fprintf(stderr,
                "FATAL: resolving network node index is too large: %llu\n",
                (unsigned long long)node_index);
        exit(1);
    }

    BitCursor cursor = bc_create(network, (size_t)node_index * 64);
    ResolvingNetworkNode node;
    node.type = bc_read_bits(&cursor, 1);
    node.data = bc_read_bits(&cursor, 15);
    node.next0 = bc_read_bits(&cursor, 16);
    node.next1 = bc_read_bits(&cursor, 16);
    node.resolver = bc_read_bits(&cursor, 16);
    return node;
}

uint16_t resolving_network_next_node(const ResolvingNetworkNode *node,
                                     uint8_t bit) {
    return bit == 0 ? node->next0 : node->next1;
}

#ifndef RESOLVING_NETWORK_H
#define RESOLVING_NETWORK_H

#include "bitstream.h"

#include <stdint.h>

typedef struct {
    int reg_num;
    uint64_t node_index;
} ResolvingNetworkEntry;

typedef struct {
    uint8_t type;       // 0 = встроенная команда, 1 = вызов процедуры
    uint16_t data;
    uint16_t next0;
    uint16_t next1;
    uint16_t resolver;
} ResolvingNetworkNode;

ResolvingNetworkNode resolving_network_decode_node(BitVector *network,
                                                    uint64_t node_index);
uint16_t resolving_network_next_node(const ResolvingNetworkNode *node,
                                     uint8_t bit);
ResolvingNetworkEntry resolving_network_next_entry(
    ResolvingNetworkEntry current, const ResolvingNetworkNode *node,
    uint8_t bit);

#endif

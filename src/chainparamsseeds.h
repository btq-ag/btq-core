#ifndef BTQ_CHAINPARAMSSEEDS_H
#define BTQ_CHAINPARAMSSEEDS_H
/**
 * List of fixed seed nodes for the BTQ network
 * These will be populated once BTQ nodes are running
 *
 * Each line contains a BIP155 serialized (networkID, addr, port) tuple.
 */

// BTQ mainnet seeds - to be added after network launch
// Format: networkID (1 byte), address length (1 byte), address, port (2 bytes big-endian)
static const uint8_t chainparams_seed_main[] = {
    0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Placeholder entry (invalid, will be ignored)
};

// BTQ testnet seeds - to be added after network launch  
static const uint8_t chainparams_seed_test[] = {
    0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Placeholder entry (invalid, will be ignored)
};

#endif // BTQ_CHAINPARAMSSEEDS_H

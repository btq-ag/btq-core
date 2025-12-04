// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_CRYPTO_DILITHIUM_HD_KEY_H
#define BTQ_CRYPTO_DILITHIUM_HD_KEY_H

#include <crypto/dilithium_key.h>
#include <crypto/dilithium_key_id.h>
#include <uint256.h>
#include <serialize.h>
#include <span.h>

#include <vector>

// Forward declarations
class DilithiumExtPubKey;

/** Dilithium HD Chain Code - 32 bytes */
typedef uint256 DilithiumChainCode;

/** Dilithium Extended Key - parallel to CExtKey but for Dilithium */
class DilithiumExtKey
{
private:
    CDilithiumKey key;
    DilithiumChainCode chaincode;
    uint8_t nDepth;
    uint8_t vchFingerprint[4];
    uint32_t nChild;

public:
    DilithiumExtKey() : nDepth(0), nChild(0) {
        memset(vchFingerprint, 0, sizeof(vchFingerprint));
    }

    // Key derivation
    bool Derive(DilithiumExtKey& out, unsigned int nChild) const;
    
    // Seed generation from BIP39 mnemonic
    void SetSeed(Span<const std::byte> seed);
    
    // Get public key
    CDilithiumPubKey GetPubKey() const { return key.GetPubKey(); }
    
    // Get private key
    const CDilithiumKey& GetPrivKey() const { return key; }
    
    // Neutering (get public-only version)
    DilithiumExtPubKey Neuter() const;
    
    // Serialization
    template <typename Stream>
    void Serialize(Stream& s) const {
        s << key;
        s << chaincode;
        s << nDepth;
        s.write(AsBytes(Span{vchFingerprint}));
        s << nChild;
    }
    
    template <typename Stream>
    void Unserialize(Stream& s) {
        s >> key;
        s >> chaincode;
        s >> nDepth;
        s.read(AsWritableBytes(Span{vchFingerprint}));
        s >> nChild;
    }
    
    // Getters
    uint8_t GetDepth() const { return nDepth; }
    const uint8_t* GetFingerprint() const { return vchFingerprint; }
    uint32_t GetChild() const { return nChild; }
    const DilithiumChainCode& GetChainCode() const { return chaincode; }
};

/** Dilithium Extended Public Key - parallel to CExtPubKey but for Dilithium */
class DilithiumExtPubKey
{
public:
    CDilithiumPubKey pubkey;
    DilithiumChainCode chaincode;
    uint8_t nDepth;
    uint8_t vchFingerprint[4];
    uint32_t nChild;
    DilithiumExtPubKey() : nDepth(0), nChild(0) {
        memset(vchFingerprint, 0, sizeof(vchFingerprint));
    }

    // Key derivation (public key only)
    bool Derive(DilithiumExtPubKey& out, unsigned int nChild) const;
    
    // Get public key
    const CDilithiumPubKey& GetPubKey() const { return pubkey; }
    
    // Serialization
    template <typename Stream>
    void Serialize(Stream& s) const {
        s << pubkey;
        s << chaincode;
        s << nDepth;
        s.write(AsBytes(Span{vchFingerprint}));
        s << nChild;
    }
    
    template <typename Stream>
    void Unserialize(Stream& s) {
        s >> pubkey;
        s >> chaincode;
        s >> nDepth;
        s.read(AsWritableBytes(Span{vchFingerprint}));
        s >> nChild;
    }
    
    // Getters
    uint8_t GetDepth() const { return nDepth; }
    const uint8_t* GetFingerprint() const { return vchFingerprint; }
    uint32_t GetChild() const { return nChild; }
    const DilithiumChainCode& GetChainCode() const { return chaincode; }
};

/** Dilithium HD Chain - parallel to CHDChain but for Dilithium */
class DilithiumHDChain
{
public:
    DilithiumKeyID seed_id;
    DilithiumExtKey master_key;
    DilithiumExtKey account_key;
    uint32_t nExternalChainCounter;
    uint32_t nInternalChainCounter;
    
    DilithiumHDChain() : nExternalChainCounter(0), nInternalChainCounter(0) {}
    
    // Serialization
    template <typename Stream>
    void Serialize(Stream& s) const {
        s << seed_id;
        s << master_key;
        s << account_key;
        s << nExternalChainCounter;
        s << nInternalChainCounter;
    }
    
    template <typename Stream>
    void Unserialize(Stream& s) {
        s >> seed_id;
        s >> master_key;
        s >> account_key;
        s >> nExternalChainCounter;
        s >> nInternalChainCounter;
    }
};

#endif // BTQ_CRYPTO_DILITHIUM_HD_KEY_H

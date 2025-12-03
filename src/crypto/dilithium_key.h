// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_CRYPTO_DILITHIUM_KEY_H
#define BTQ_CRYPTO_DILITHIUM_KEY_H

#include <serialize.h>
#include <support/allocators/secure.h>
#include <uint256.h>
#include <span.h>
#include <key.h>

#include <array>
#include <memory>
#include <vector>

// Dilithium extended key size: 1 (depth) + 4 (fingerprint) + 4 (child) + 32 (chaincode) + 2560 (private key) = 2601 bytes
const unsigned int DILITHIUM_EXTKEY_SIZE = 2601;
// Dilithium extended pubkey size: 1 (depth) + 4 (fingerprint) + 4 (child) + 32 (chaincode) + 1952 (public key) = 1993 bytes  
const unsigned int DILITHIUM_EXTPUBKEY_SIZE = 1993;

// Forward declarations for Dilithium C API
extern "C" {
#include "dilithium_wrapper.h"
}

/** Size constants for different Dilithium variants */
namespace DilithiumConstants {
    // Dilithium2 (default mode)
    constexpr size_t DILITHIUM2_PUBLIC_KEY_SIZE = 1312;
    constexpr size_t DILITHIUM2_SECRET_KEY_SIZE = 2560;
    constexpr size_t DILITHIUM2_SIGNATURE_SIZE = 2420;
    
    // Dilithium5 (high security)
    constexpr size_t DILITHIUM5_PUBLIC_KEY_SIZE = 2592;
    constexpr size_t DILITHIUM5_SECRET_KEY_SIZE = 4896;
    constexpr size_t DILITHIUM5_SIGNATURE_SIZE = 4627;
    
    // Use Dilithium2 as default (can be configured via DILITHIUM_MODE)
    constexpr size_t PUBLIC_KEY_SIZE = BTQ_DILITHIUM_PUBLIC_KEY_SIZE;
    constexpr size_t SECRET_KEY_SIZE = BTQ_DILITHIUM_SECRET_KEY_SIZE;
    constexpr size_t SIGNATURE_SIZE = BTQ_DILITHIUM_SIGNATURE_SIZE;
}

class CDilithiumPubKey; // Forward declaration
class CDilithiumExtPubKey; // Forward declaration

/** An encapsulated Dilithium private key. */
class CDilithiumKey
{
public:
    /** Maximum size for serialized signatures */
    static constexpr size_t MAX_SIGNATURE_SIZE = DilithiumConstants::SIGNATURE_SIZE;

private:
    /** Internal data container for private key material. */
    using KeyType = std::array<unsigned char, DilithiumConstants::SECRET_KEY_SIZE + DilithiumConstants::PUBLIC_KEY_SIZE>;

    /** The actual private key data. nullptr for invalid keys. */
    secure_unique_ptr<KeyType> keydata;

    /** Ensure keydata is allocated */
    void MakeKeyData()
    {
        if (!keydata) keydata = make_secure_unique<KeyType>();
    }

    /** Clear key data securely */
    void ClearKeyData()
    {
        keydata.reset();
    }

public:
    /** Default constructor - creates invalid key */
    CDilithiumKey() noexcept = default;
    
    /** Move constructor */
    CDilithiumKey(CDilithiumKey&&) noexcept = default;
    
    /** Move assignment */
    CDilithiumKey& operator=(CDilithiumKey&&) noexcept = default;

    /** Copy assignment */
    CDilithiumKey& operator=(const CDilithiumKey& other)
    {
        if (other.keydata) {
            MakeKeyData();
            *keydata = *other.keydata;
        } else {
            ClearKeyData();
        }
        return *this;
    }

    /** Copy constructor */
    CDilithiumKey(const CDilithiumKey& other) { *this = other; }

    /** Equality operator */
    friend bool operator==(const CDilithiumKey& a, const CDilithiumKey& b)
    {
        return a.size() == b.size() &&
               (a.size() == 0 || memcmp(a.data(), b.data(), a.size()) == 0);
    }

    /** Inequality operator */
    friend bool operator!=(const CDilithiumKey& a, const CDilithiumKey& b)
    {
        return !(a == b);
    }

    //! Initialize using begin and end iterators to byte data.
    template <typename T>
    void Set(const T pbegin, const T pend)
    {
        if (size_t(pend - pbegin) != KeyType{}.size()) {
            ClearKeyData();
        } else {
            MakeKeyData();
            memcpy(keydata->data(), (unsigned char*)&pbegin[0], keydata->size());
        }
    }

    //! Simple read-only vector-like interface.
    unsigned int size() const { return keydata ? keydata->size() : 0; }
    const std::byte* data() const { return keydata ? reinterpret_cast<const std::byte*>(keydata->data()) : nullptr; }
    const unsigned char* begin() const { return keydata ? keydata->data() : nullptr; }
    const unsigned char* end() const { return begin() + size(); }

    //! Check whether this private key is valid.
    bool IsValid() const { return !!keydata; }

    //! Generate a new private key using a cryptographic PRNG.
    void MakeNewKey();

    /**
     * Generate a new private key from provided entropy.
     * This is used for deterministic key derivation in HD wallets.
     * @param entropy 32 bytes of entropy for key generation
     * @return true if key was generated successfully
     */
    bool GenerateFromEntropy(const std::vector<unsigned char>& entropy);

    /**
     * Compute the public key from this private key.
     * This is expensive but necessary for Dilithium.
     */
    CDilithiumPubKey GetPubKey() const;

    /**
     * Create a Dilithium signature.
     * @param hash The message hash to sign (typically 32 bytes)
     * @param vchSig Output vector for the signature
     * @param context Optional context string for domain separation
     * @return true if signature was created successfully
     */
    bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig, 
              const std::vector<unsigned char>& context = {}) const;

    /**
     * Create a Dilithium signature for arbitrary message.
     * @param message The message to sign
     * @param vchSig Output vector for the signature
     * @param context Optional context string for domain separation
     * @return true if signature was created successfully
     */
    bool SignMessage(Span<const unsigned char> message, std::vector<unsigned char>& vchSig,
                     const std::vector<unsigned char>& context = {}) const;

    /**
     * Verify that this private key corresponds to the given public key.
     * This is done by deriving the public key and comparing.
     */
    bool VerifyPubKey(const CDilithiumPubKey& pubkey) const;

    //! Load private key from raw bytes
    bool Load(Span<const unsigned char> privkey);

    //! Serialize the private key (for wallet storage, etc.)
    std::vector<unsigned char> Serialize() const;

    //! Get key size in bytes
    static constexpr size_t GetKeySize() { return DilithiumConstants::SECRET_KEY_SIZE + DilithiumConstants::PUBLIC_KEY_SIZE; }
    
    //! Get public key size in bytes
    static constexpr size_t GetPubKeySize() { return DilithiumConstants::PUBLIC_KEY_SIZE; }
    
    //! Get private key as CPrivKey
    CPrivKey GetPrivKey() const
    {
        CPrivKey privkey;
        if (IsValid()) {
            privkey.assign(begin(), end());
        }
        return privkey;
    }
};

/** An encapsulated Dilithium public key. */
class CDilithiumPubKey
{
public:
    /** Size constants */
    static constexpr size_t SIZE = DilithiumConstants::PUBLIC_KEY_SIZE;
    static constexpr size_t SIGNATURE_SIZE = DilithiumConstants::SIGNATURE_SIZE;

private:
    /** Public key data storage */
    std::array<unsigned char, SIZE> vch;

    //! Set this key data to be invalid
    void Invalidate()
    {
        vch.fill(0);
    }

public:
    //! Construct an invalid public key.
    CDilithiumPubKey()
    {
        Invalidate();
    }

    //! Initialize a public key using begin/end iterators to byte data.
    template <typename T>
    void Set(const T pbegin, const T pend)
    {
        if (size_t(pend - pbegin) == SIZE) {
            memcpy(vch.data(), (unsigned char*)&pbegin[0], SIZE);
        } else {
            Invalidate();
        }
    }

    //! Construct a public key using begin/end iterators to byte data.
    template <typename T>
    CDilithiumPubKey(const T pbegin, const T pend)
    {
        Set(pbegin, pend);
    }

    //! Construct a public key from a byte span.
    explicit CDilithiumPubKey(Span<const uint8_t> _vch)
    {
        Set(_vch.begin(), _vch.end());
    }

    //! Simple read-only vector-like interface to the pubkey data.
    unsigned int size() const { return SIZE; }
    const unsigned char* data() const { return vch.data(); }
    const unsigned char* begin() const { return vch.data(); }
    const unsigned char* end() const { return vch.data() + SIZE; }
    const unsigned char& operator[](unsigned int pos) const { return vch[pos]; }

    //! Comparator implementations.
    friend bool operator==(const CDilithiumPubKey& a, const CDilithiumPubKey& b)
    {
        return memcmp(a.vch.data(), b.vch.data(), SIZE) == 0;
    }
    
    friend bool operator!=(const CDilithiumPubKey& a, const CDilithiumPubKey& b)
    {
        return !(a == b);
    }
    
    friend bool operator<(const CDilithiumPubKey& a, const CDilithiumPubKey& b)
    {
        return memcmp(a.vch.data(), b.vch.data(), SIZE) < 0;
    }

    //! Implement serialization
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        s << Span{vch};
    }
    
    template <typename Stream>
    void Unserialize(Stream& s)
    {
        s >> Span{vch};
    }

    //! Get a hash of this public key for use as identifier
    uint256 GetHash() const;

    //! Get the ID of this public key (hash of its serialization)
    uint160 GetID() const;

    /**
     * Check syntactic correctness.
     * A Dilithium public key is valid if it's not all zeros.
     */
    bool IsValid() const;

    /**
     * Fully validate whether this is a valid Dilithium public key.
     * This performs more extensive validation than IsValid().
     */
    bool IsFullyValid() const;

    /**
     * Verify a Dilithium signature against this public key.
     * @param hash The message hash that was signed
     * @param vchSig The signature to verify
     * @param context Optional context string used during signing
     * @return true if signature is valid
     */
    bool Verify(const uint256& hash, const std::vector<unsigned char>& vchSig,
                const std::vector<unsigned char>& context = {}) const;

    /**
     * Verify a Dilithium signature for arbitrary message.
     * @param message The original message that was signed
     * @param vchSig The signature to verify
     * @param context Optional context string used during signing
     * @return true if signature is valid
     */
    bool VerifyMessage(Span<const unsigned char> message, const std::vector<unsigned char>& vchSig,
                       const std::vector<unsigned char>& context = {}) const;

    //! Derive address from this public key (for Bitcoin address generation)
    std::vector<unsigned char> GetAddress() const;
};

/** Dilithium extended key for HD wallet support */
class CDilithiumExtKey {
public:
    unsigned char nDepth;
    unsigned char vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CDilithiumKey key;

    friend bool operator==(const CDilithiumExtKey& a, const CDilithiumExtKey& b)
    {
        return a.nDepth == b.nDepth &&
            memcmp(a.vchFingerprint, b.vchFingerprint, sizeof(vchFingerprint)) == 0 &&
            a.nChild == b.nChild &&
            a.chaincode == b.chaincode &&
            a.key == b.key;
    }

    void Encode(unsigned char code[DILITHIUM_EXTKEY_SIZE]) const;
    void Decode(const unsigned char code[DILITHIUM_EXTKEY_SIZE]);
    [[nodiscard]] bool Derive(CDilithiumExtKey& out, unsigned int nChild) const;
    CDilithiumExtPubKey Neuter() const;
    void SetSeed(Span<const std::byte> seed);
};

/** Dilithium extended public key for HD wallet support */
class CDilithiumExtPubKey {
public:
    unsigned char nDepth;
    unsigned char vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CDilithiumPubKey pubkey;

    friend bool operator==(const CDilithiumExtPubKey& a, const CDilithiumExtPubKey& b)
    {
        return a.nDepth == b.nDepth &&
            memcmp(a.vchFingerprint, b.vchFingerprint, sizeof(vchFingerprint)) == 0 &&
            a.nChild == b.nChild &&
            a.chaincode == b.chaincode &&
            a.pubkey == b.pubkey;
    }

    void Encode(unsigned char code[DILITHIUM_EXTKEY_SIZE]) const;
    void Decode(const unsigned char code[DILITHIUM_EXTKEY_SIZE]);
    [[nodiscard]] bool Derive(CDilithiumExtPubKey& out, unsigned int nChild) const;
};

/** Initialize the Dilithium cryptographic support. */
void DilithiumInit();

/** Check that required Dilithium support is available at runtime. */
bool DilithiumSanityCheck();

#endif // BTQ_CRYPTO_DILITHIUM_KEY_H

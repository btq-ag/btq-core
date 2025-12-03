// Copyright (c) 2013-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <clientversion.h>
#include <key.h>
#include <key_io.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <string>
#include <vector>

namespace {

struct TestDerivation {
    std::string pub;
    std::string prv;
    unsigned int nChild;
};

struct TestVector {
    std::string strHexMaster;
    std::vector<TestDerivation> vDerive;

    explicit TestVector(std::string strHexMasterIn) : strHexMaster(strHexMasterIn) {}

    TestVector& operator()(std::string pub, std::string prv, unsigned int nChild) {
        vDerive.emplace_back();
        TestDerivation &der = vDerive.back();
        der.pub = pub;
        der.prv = prv;
        der.nChild = nChild;
        return *this;
    }
};

TestVector test1 =
  TestVector("000102030405060708090a0b0c0d0e0f")
    ("xpubEPi3iGSX9RiyvsV1Di18LRuDrFpz6df7c66p4wnNJAPnoasbg8Cz2EL4st4MxPJkjGD2cuow7PNo7bnjvJiKATe4D5SsVPBpUxLzYWtrgz1",
     "xprvJAihJkudK4AgiPQY7gU7yHxVJDzVhAwGEsBDGZNkjprovnYT8atjUS1b2cR4SVtgojK67xnnU62VK5bbd4sfYRdyRNieKrgDiqkFWCEWExN",
     0x80000000)
    ("xpubERyThyjzEgqy5UciRTsmDU1LB5PLUi98HhXi33AVGMb5AsAn6Mk59NgUf9d4hhDHjUVbV9MMr2St5zkssLShEM6nQiF8Qh62ztUYrFVaZFk",
     "xprvJCz7JUD6QKHfrzYFKSLkrL4bd3Yr5FRGvUc7Eeksi246J4qdYpRpbaMzosmPrDGeWnCnVG7erSsN2qdKGf9rLViVVFPnZpP7RrP6wMnupjL",
     1)
    ("xpubEU9aumHsdPj2uvfA9imdpCtzETNSUjsRE2CYdJwJCzPHQQfupAAP6wai5E5e7dvdhC7eGZoTxcwMgJVhLfcauzvihv5ytL6Jdd1L9BZj43X",
     "xprvJFAEWFkyo2AjhSah3hEdT4xFgRXx5H9ZroGwpvXgeerJXcLmGcr8Z9GEDvxTT93gxrxUJ5i5w9pnAn9YPzwq7fCw12MWda9y6ehUUKeaq3U",
     0x80000002)
    ("xpubEWkrxJ7jLGaSn6TFQKB1BCw8TcExkHTQZZSDseETNrkipFs2cuzp1P8ddFGUZWMx3iDNswn5LZWL8ddz3mA7yT5ZHp47yH9zMJvUESQties",
     "xprvJHmWYnaqVu29ZcNnJHdzp4zPuaQULpjZCLWd5FpqpXDjwTXt5NgZTap9myB5RNZBUKyrtYf1MZegPytHz1s2EkebMdv4nWPuhvSh6xDUSvj",
     2)
    ("xpubEYzFnjEgWkFRrZvy6aP64i6SYRXhNsmc13KX8bkvhJKLBMVPgghBCxbZJaFryJWgX2LZ1xG2PJQSh3yz94bKh3YpCbcJoU3pMzqKP1ibz67",
     "xprvJKzuPDhngNh8e5rVzYr5ha9hzPhCyR3kdpPvLDMK8xnMJZAF99NvfAH5THaXt2xcY87ZWTtaGP6qsAnkBSQNS9Edkt1ALbDcrturcP6oxNF",
     1000000000)
    ("xpubEai2GQqvdsddNuiRD7wpNR6SLf71CNadoHi5Nt94uNhptUtnD6wqU4kcwQAopZbPzid6XJBpvW73HUP1Ev6AYJhw592PbwdTAxTEdAnjzq4",
     "xprvJMifruK2oW5LARdx76Qp1H9hndGWnurnS4nUaVjTM3Ar1gZdfZdavGS969Mjnx5h9XWpHpsX7RdxsLo7iWR6Z6tB6NmPTVyL9EUDMXsWgpq",
     5);

TestVector test2 =
  TestVector("fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542")
    ("xpubEPi3iGSX9RiyvUzLKs5Qndcz6SyQnFuwHftYMtXCGYWf6ZtnrGyvT8VfDz9LfdfidpSpXaU6WTt3qPWy3VdUJNe151eLKsgUoL26pVsNi6y",
     "xprvJAihJkudK4AghzusDqYQRVgFYR8vNoC5vSxwZW7aiCygDmZeJjffuLBBNgC7d1Sn3n4WsFFEkC4gQcKhfVGN9nxjsnntUCaxJ6NUsCHCVhk",
     0)
    ("xpubESynyz8UdSZ4SkadTeTHrS2WTQifVEQDqqduXhYeLxNcnYLmRYKzby4uoT7ETig9r8vzPoTquYj1zYweveKtdyJHM4P1EDprvp8mFgq3AcG",
     "xprvJDzSaUbao4zmEGWAMcvHVJ5muNtB5mgNUciJjK92ncqduk1ct11k4AkRxBTvG5HcTSJLmFeyBMUom7xXDmmR6288syCwrgkhRWH3ETxFZv9",
     0xFFFFFFFF)
    ("xpubEU8rEbA11LjAccnot6SwKmi4r9wa2iZuvziST6TQzh91EqaE3RkJp2rWQqMMDTHFderY3eKTCm9cfDozhYp1N7kut914ax5soCQN6TVszW4",
     "xprvJF9Vq5d7AyAsQ8iLn4uvxdmLJ875dFr4Zmnqei3oSMc2N3F5VtS4GEY2ZXwtzL26MfdEYGrcEMyvCMhHx9NYfKZhJhSHdCUxDHcZzssRSbC",
     1)
    ("xpubEWwpec8wAz2MiETwwkbWHieJBgp58uVBFSDJJA7kJaVSufSPLwH1hwEtFdYV1KyYDqK5tHQgqtTSk251cTExdTDpURCuoVX5iy5KLyMdw93",
     "xprvJHxUF6c3LcU4VkPUqj4VvahZdeyajSmKtDHhVmi8kExU2s7EoPxmA8vQQL9X23DnfcywHZvzduf8X4UT12tvfoNzSTy6UJTLh63JUZkZUFB",
     0xFFFFFFFE)
    ("xpubEY7rZa5HnNz51BhYgER3fELV6RHSBt7gH8wBpspA9LkhN1mKScD2ETbytHfzChVzBGxignUPtVnJPfEQJLMULovGkKXQMncVbC9tssm5hxR",
     "xprvJK8WA4YPx1Rmnhd5aCt3J6PkYPSwnRPpuv1b2VQYb1DiVDSAu4tmgfHW32sueTc5DCGdbPhz2HEiLBZpWyoRMe4AGQWQcWUy2g3pYMXLCqJ",
     2)
    ("xpubEZUtX1HoJZJJkStSQJYjdyiTVUpYSD2kwJZ5EfAen8cHSD6tcF8nR2YyxgkGLGrghLAwxhyonCoHt6Hu8S3Nf4mrPVMVJ9uEGVM5YbU4Yn9",
     "xprvJLVY7VkuUBk1XxoyJH1jGqmiwSz42kJua5dUSGm3Do5JZQmk4hpXsEEW7SYuoUh9FW8Mt9LoUP5QNjTLFJAeQcnt5G1ufvcgj3PNctL86YB",
     0);

TestVector test3 =
  TestVector("4b381541583be4423346c643850da4b320e46a87ae3d2a4e6da11eba819cd4acba45d239319ac14f863b8d5ab5a0d0c64d2e8a1e7d1457df2e5a3c51c73235be")
    ("xpubEPi3iGSX9RiyuYSVrFaCzct1y6JWY85D11f6mBYkYEbT4p84WRDtQPjBhygKVEFMouCgMaM6u8huGh9stCfuiiJ9CFYZ4S8Kb3XoLvwVK39",
     "xprvJAihJkudK4Agh4N2kE3CdUwHR4U28fMMdnjVxo98yu4UC1nuxsudrbQhrfvHa2g78UjGSAwLecoPmFw1tvQnwGTzQ5MXfdvvap2qX7VLMQH",
      0x80000000)
    ("xpubES5FTEGepo6L75Xu5Pui3sdgfSutgyV7QVu3aREbUYWpKqFX5uzkUrWu1hWqDar71GhX8Y6qk5uPuA6DpSc6Hxa5sMVScHd85Dti1PLxXyh",
     "xprvJD5u3ijkzRY2tbTRyNNhgjgx7R5QHWmG3GySn2pyvCyqT2vNYNgVw4CRASGduz12adNgT7jDJMRrQoaAU8zYh2fcqDGaQoCjhpzVUU7J7iM",
      0);

TestVector test4 =
  TestVector("3ddd5602285899a946114506157c7997e5444528f3003f6134712147db19b678")
    ("xpubEPi3iGSX9Riywbx4gyw1oNLubKtgTNUxF9MW8vELQmS8uhwPiKpeccHHQ6vzWfqRPSLEWFaHquUe5Zw1JQgpoaA3xfqdck68VeD2RkSZk8X",
     "xprvJAihJkudK4Agj7sbaxQ1SEQB3J4C3um6svRuLXpirRuA2ucFAnWQ4oxoYqCQam5HhwgtYMpUax1AQGZJZ1fi268NuKDZiWparwtzHLGUKkg",
     0x80000000)
    ("xpubESsA6eZBXBqRaUyCKJrnomSg3FmxAg1VzSyAUtQK4DW7QEzWGfFNkexdZhCkYJCYoY6bTHxuSqQtcVd51C9DttD5zzGbVZBWYSm5EDBw3ue",
     "xprvJDsoh92HgpH8MztjDHKnSdVwVDwTmDHedE3ZgVzhVsy8XSfMj7w8Cre9iP7edaFefviL4vzhuQ7FLvkQAPLM2j1JkhXEvSGarCvo29gz5YL",
     0x80000001)
    ("xpubEUzqkdxC2ut2FdpqTamgue2XDBx2fbiJeP1zFdvWiQbZBLK299j5ymvWTdyHPUqEPKhxM4u1uHksaywyTnxNcB9TLn7eTHfTQs7yHiTr4Tb",
     "xprvJG1VM8RJCYKj39kNMZEgYW5nfA7YG8zTHA6PTFWuA54aJXysbcQqRyc2cM5jgyHUkh7SFXpfg3D83ta7ZKHUK7UY13EG2UWuhtUV7gxDpB6",
     0);

const std::vector<std::string> TEST5 = {
    "xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6LBpB85b3D2yc8sfvZU521AAwdZafEz7mnzBBsz4wKY5fTtTQBm",
    "xprv9s21ZrQH143K24Mfq5zL5MhWK9hUhhGbd45hLXo2Pq2oqzMMo63oStZzFGTQQD3dC4H2D5GBj7vWvSQaaBv5cxi9gafk7NF3pnBju6dwKvH",
    "xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6Txnt3siSujt9RCVYsx4qHZGc62TG4McvMGcAUjeuwZdduYEvFn",
    "xprv9s21ZrQH143K24Mfq5zL5MhWK9hUhhGbd45hLXo2Pq2oqzMMo63oStZzFGpWnsj83BHtEy5Zt8CcDr1UiRXuWCmTQLxEK9vbz5gPstX92JQ",
    "xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6N8ZMMXctdiCjxTNq964yKkwrkBJJwpzZS4HS2fxvyYUA4q2Xe4",
    "xprv9s21ZrQH143K24Mfq5zL5MhWK9hUhhGbd45hLXo2Pq2oqzMMo63oStZzFAzHGBP2UuGCqWLTAPLcMtD9y5gkZ6Eq3Rjuahrv17fEQ3Qen6J",
    "xprv9s2SPatNQ9Vc6GTbVMFPFo7jsaZySyzk7L8n2uqKXJen3KUmvQNTuLh3fhZMBoG3G4ZW1N2kZuHEPY53qmbZzCHshoQnNf4GvELZfqTUrcv",
    "xpub661no6RGEX3uJkY4bNnPcw4URcQTrSibUZ4NqJEw5eBkv7ovTwgiT91XX27VbEXGENhYRCf7hyEbWrR3FewATdCEebj6znwMfQkhRYHRLpJ",
    "xprv9s21ZrQH4r4TsiLvyLXqM9P7k1K3EYhA1kkD6xuquB5i39AU8KF42acDyL3qsDbU9NmZn6MsGSUYZEsuoePmjzsB3eFKSUEh3Gu1N3cqVUN",
    "xpub661MyMwAuDcm6CRQ5N4qiHKrJ39Xe1R1NyfouMKTTWcguwVcfrZJaNvhpebzGerh7gucBvzEQWRugZDuDXjNDRmXzSZe4c7mnTK97pTvGS8",
    "DMwo58pR1QLEFihHiXPVykYB6fJmsTeHvyTp7hRThAtCX8CvYzgPcn8XnmdfHGMQzT7ayAmfo4z3gY5KfbrZWZ6St24UVf2Qgo6oujFktLHdHY4",
    "DMwo58pR1QLEFihHiXPVykYB6fJmsTeHvyTp7hRThAtCX8CvYzgPcn8XnmdfHPmHJiEDXkTiJTVV9rHEBUem2mwVbbNfvT2MTcAqj3nesx8uBf9",
    "xprv9s21ZrQH143K24Mfq5zL5MhWK9hUhhGbd45hLXo2Pq2oqzMMo63oStZzF93Y5wvzdUayhgkkFoicQZcP3y52uPPxFnfoLZB21Teqt1VvEHx",
    "xprv9s21ZrQH143K24Mfq5zL5MhWK9hUhhGbd45hLXo2Pq2oqzMMo63oStZzFAzHGBP2UuGCqWLTAPLcMtD5SDKr24z3aiUvKr9bJpdrcLg1y3G",
    "xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6Q5JXayek4PRsn35jii4veMimro1xefsM58PgBMrvdYre8QyULY",
    "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHL"
};

void RunTest(const TestVector& test)
{
    std::vector<std::byte> seed{ParseHex<std::byte>(test.strHexMaster)};
    CExtKey key;
    CExtPubKey pubkey;
    key.SetSeed(seed);
    pubkey = key.Neuter();
    for (const TestDerivation &derive : test.vDerive) {
        unsigned char data[74];
        key.Encode(data);
        pubkey.Encode(data);

        // Test private key
        std::string encoded_key = EncodeExtKey(key);
        std::string expected_prv = derive.prv;
        if (encoded_key != expected_prv) {
            std::cout << "BIP32 Debug - Private Key Encoding Mismatch:" << std::endl;
            std::cout << "  Expected: " << expected_prv << std::endl;
            std::cout << "  Got:      " << encoded_key << std::endl;
        }
        BOOST_CHECK(encoded_key == expected_prv);
        
        CExtKey decoded_key = DecodeExtKey(derive.prv);
        if (!(decoded_key == key)) {
            std::cout << "BIP32 Debug - Private Key Decoding Mismatch:" << std::endl;
            std::cout << "  Original key depth: " << (int)key.nDepth << std::endl;
            std::cout << "  Decoded key depth: " << (int)decoded_key.nDepth << std::endl;
            std::cout << "  Original child: " << key.nChild << std::endl;
            std::cout << "  Decoded child: " << decoded_key.nChild << std::endl;
        }
        BOOST_CHECK(decoded_key == key); //ensure a base58 decoded key also matches

        // Test public key
        std::string encoded_pubkey = EncodeExtPubKey(pubkey);
        std::string expected_pub = derive.pub;
        if (encoded_pubkey != expected_pub) {
            std::cout << "BIP32 Debug - Public Key Encoding Mismatch:" << std::endl;
            std::cout << "  Expected: " << expected_pub << std::endl;
            std::cout << "  Got:      " << encoded_pubkey << std::endl;
        }
        BOOST_CHECK(encoded_pubkey == expected_pub);
        
        CExtPubKey decoded_pubkey = DecodeExtPubKey(derive.pub);
        if (!(decoded_pubkey == pubkey)) {
            std::cout << "BIP32 Debug - Public Key Decoding Mismatch:" << std::endl;
            std::cout << "  Original pubkey depth: " << (int)pubkey.nDepth << std::endl;
            std::cout << "  Decoded pubkey depth: " << (int)decoded_pubkey.nDepth << std::endl;
            std::cout << "  Original child: " << pubkey.nChild << std::endl;
            std::cout << "  Decoded child: " << decoded_pubkey.nChild << std::endl;
        }
        BOOST_CHECK(decoded_pubkey == pubkey); //ensure a base58 decoded pubkey also matches

        // Derive new keys
        CExtKey keyNew;
        BOOST_CHECK(key.Derive(keyNew, derive.nChild));
        CExtPubKey pubkeyNew = keyNew.Neuter();
        if (!(derive.nChild & 0x80000000)) {
            // Compare with public derivation
            CExtPubKey pubkeyNew2;
            BOOST_CHECK(pubkey.Derive(pubkeyNew2, derive.nChild));
            BOOST_CHECK(pubkeyNew == pubkeyNew2);
        }
        key = keyNew;
        pubkey = pubkeyNew;
    }
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(bip32_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bip32_test1) {
    RunTest(test1);
}

BOOST_AUTO_TEST_CASE(bip32_test2) {
    RunTest(test2);
}

BOOST_AUTO_TEST_CASE(bip32_test3) {
    RunTest(test3);
}

BOOST_AUTO_TEST_CASE(bip32_test4) {
    RunTest(test4);
}

BOOST_AUTO_TEST_CASE(bip32_test5) {
    for (const auto& str : TEST5) {
        auto dec_extkey = DecodeExtKey(str);
        auto dec_extpubkey = DecodeExtPubKey(str);
        BOOST_CHECK_MESSAGE(!dec_extkey.key.IsValid(), "Decoding '" + str + "' as xprv should fail");
        BOOST_CHECK_MESSAGE(!dec_extpubkey.pubkey.IsValid(), "Decoding '" + str + "' as xpub should fail");
    }
}

BOOST_AUTO_TEST_CASE(bip32_max_depth) {
    CExtKey key_parent{DecodeExtKey(test1.vDerive[0].prv)}, key_child;
    CExtPubKey pubkey_parent{DecodeExtPubKey(test1.vDerive[0].pub)}, pubkey_child;

    // We can derive up to the 255th depth..
    for (auto i = 0; i++ < 255;) {
        BOOST_CHECK(key_parent.Derive(key_child, 0));
        std::swap(key_parent, key_child);
        BOOST_CHECK(pubkey_parent.Derive(pubkey_child, 0));
        std::swap(pubkey_parent, pubkey_child);
    }

    // But trying to derive a non-existent 256th depth will fail!
    BOOST_CHECK(key_parent.nDepth == 255 && pubkey_parent.nDepth == 255);
    BOOST_CHECK(!key_parent.Derive(key_child, 0));
    BOOST_CHECK(!pubkey_parent.Derive(pubkey_child, 0));
}

BOOST_AUTO_TEST_SUITE_END()

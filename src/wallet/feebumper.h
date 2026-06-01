// Copyright (c) 2017-2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_WALLET_FEEBUMPER_H
#define BTQ_WALLET_FEEBUMPER_H

#include <consensus/consensus.h>
#include <crypto/dilithium_key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/interpreter.h>

class uint256;
enum class FeeEstimateMode;
struct bilingual_str;

namespace wallet {
class CCoinControl;
class CWallet;
class CWalletTx;

namespace feebumper {

enum class Result
{
    OK,
    INVALID_ADDRESS_OR_KEY,
    INVALID_REQUEST,
    INVALID_PARAMETER,
    WALLET_ERROR,
    MISC_ERROR,
};

//! Return whether transaction can be bumped.
bool TransactionCanBeBumped(const CWallet& wallet, const uint256& txid);

/** Create bumpfee transaction based on feerate estimates.
 *
 * @param[in] wallet The wallet to use for this bumping
 * @param[in] txid The txid of the transaction to bump
 * @param[in] coin_control A CCoinControl object which provides feerates and other information used for coin selection
 * @param[out] errors Errors
 * @param[out] old_fee The fee the original transaction pays
 * @param[out] new_fee the fee that the bump transaction pays
 * @param[out] mtx The bump transaction itself
 * @param[in] require_mine Whether the original transaction must consist of inputs that can be spent by the wallet
 * @param[in] outputs Vector of new outputs to replace the bumped transaction's outputs
 * @param[in] original_change_index The position of the change output to deduct the fee from in the transaction being bumped
 */
Result CreateRateBumpTransaction(CWallet& wallet,
    const uint256& txid,
    const CCoinControl& coin_control,
    std::vector<bilingual_str>& errors,
    CAmount& old_fee,
    CAmount& new_fee,
    CMutableTransaction& mtx,
    bool require_mine,
    const std::vector<CTxOut>& outputs,
    std::optional<uint32_t> original_change_index = std::nullopt);

//! Sign the new transaction,
//! @return false if the tx couldn't be found or if it was
//! impossible to create the signature(s)
bool SignTransaction(CWallet& wallet, CMutableTransaction& mtx);

//! Commit the bumpfee transaction.
//! @return success in case of CWallet::CommitTransaction was successful,
//! but sets errors if the tx could not be added to the mempool (will try later)
//! or if the old transaction could not be marked as replaced.
Result CommitTransaction(CWallet& wallet,
    const uint256& txid,
    CMutableTransaction&& mtx,
    std::vector<bilingual_str>& errors,
    uint256& bumped_txid);

struct SignatureWeights
{
private:
    int64_t m_max_sigs_weight{0};
    int64_t m_sigs_weight{0};

public:
    void AddSigWeight(const size_t weight, const SigVersion sigversion, const size_t max_weight = CPubKey::SIGNATURE_SIZE)
    {
        int64_t scale{0};
        switch (sigversion) {
        case SigVersion::BASE:
            scale = WITNESS_SCALE_FACTOR;
            break;
        case SigVersion::WITNESS_V0:
        case SigVersion::TAPROOT:
        case SigVersion::TAPSCRIPT:
        case SigVersion::P2MR_TAPSCRIPT:
            scale = 1;
            break;
        }
        m_sigs_weight += weight * scale;
        m_max_sigs_weight += max_weight * scale;
    }

    int64_t GetWeightDiffToMax() const
    {
        return m_max_sigs_weight - m_sigs_weight;
    }
};

class SignatureWeightChecker : public DeferringSignatureChecker
{
private:
    SignatureWeights& m_weights;

public:
    SignatureWeightChecker(SignatureWeights& weights, const BaseSignatureChecker& checker) : DeferringSignatureChecker(checker), m_weights(weights) {}

    bool CheckECDSASignature(const std::vector<unsigned char>& sig, const std::vector<unsigned char>& pubkey, const CScript& script, SigVersion sigversion) const override
    {
        if (m_checker.CheckECDSASignature(sig, pubkey, script, sigversion)) {
            m_weights.AddSigWeight(sig.size(), sigversion);
            return true;
        }
        return false;
    }

    bool CheckSchnorrSignature(Span<const unsigned char> sig, Span<const unsigned char> pubkey, SigVersion sigversion, ScriptExecutionData& execdata, ScriptError* serror = nullptr) const override
    {
        if (m_checker.CheckSchnorrSignature(sig, pubkey, sigversion, execdata, serror)) {
            m_weights.AddSigWeight(sig.size(), sigversion, /* max_weight=*/65);
            return true;
        }
        return false;
    }

    bool CheckDilithiumSignature(const std::vector<unsigned char>& sig, const std::vector<unsigned char>& pubkey, const CScript& script, SigVersion sigversion) const override
    {
        if (m_checker.CheckDilithiumSignature(sig, pubkey, script, sigversion)) {
            m_weights.AddSigWeight(sig.size(), sigversion, DilithiumConstants::SIGNATURE_SIZE + 1);
            return true;
        }
        return false;
    }
};

} // namespace feebumper
} // namespace wallet

#endif // BTQ_WALLET_FEEBUMPER_H

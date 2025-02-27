// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <keystore.h>
#include <random.h>
#include <script/sign.h>
#include <sidechain.h>
#include <uint256.h>
#include <utilstrencodings.h>
#include <validation.h>

#include <test/test_drivechain.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bmm_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(bmm_commit)
{
    // Create a BMM h* request transaction
    // Create critical data
    CScript bytes;
    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0);

    CCriticalData criticalData;
    criticalData.vBytes = std::vector<unsigned char>(bytes.begin(), bytes.end());
    criticalData.hashCritical = GetRandHash();

    // Create transaction with critical data
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vin[0].prevout.hash = GetRandHash();
    mtx.vin[0].prevout.n = 0;
    mtx.vout[0].scriptPubKey = CScript() << OP_0;
    mtx.vout[0].nValue = 50 * CENT;

    // Set locktime to the block we would like critical data to be commited in
    mtx.nLockTime = 102;

    // Add critical data
    mtx.criticalData = criticalData;

    // Create dummy coinbase
    CMutableTransaction coinbase;
    coinbase.nVersion = 1;
    coinbase.vin.resize(1);
    coinbase.vout.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 102;

    // Add dummy coinbase & critical data tx to block
    CBlock block;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.push_back(MakeTransactionRef(std::move(mtx)));

    // Generate commit
    GenerateCriticalHashCommitments(block);

    BOOST_REQUIRE(block.vtx[0]->vout.size() == 2);

    // Check that the commit has been generated
    uint256 hashCritical;
    std::vector<unsigned char> vBytes;
    BOOST_CHECK(block.vtx[0]->vout[1].scriptPubKey.IsCriticalHashCommit(hashCritical, vBytes));
    BOOST_CHECK(hashCritical == criticalData.hashCritical);
}

BOOST_AUTO_TEST_CASE(bmm_commit_format)
{
    // Test the IsBMMCommitment function with many different BMM requests
    CCriticalData bmm;

    // Null
    BOOST_CHECK(!bmm.IsBMMRequest());

    // Null bytes
    bmm.hashCritical = GetRandHash();
    BOOST_CHECK(!bmm.IsBMMRequest());

    // Null h*
    bmm.hashCritical.SetNull();
    bmm.vBytes = std::vector<unsigned char> {0x00};
    BOOST_CHECK(!bmm.IsBMMRequest());

    // With valid h*, invalid bytes
    bmm.hashCritical = GetRandHash();
    BOOST_CHECK(!bmm.IsBMMRequest());

    // Null h*, valid bytes
    bmm.hashCritical.SetNull();

    CScript bytes;
    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0); // nSidechain
    bytes.push_back(0xFD);
    bytes.push_back(0xFD);
    bytes.push_back(0xFD);
    bytes.push_back(0xFD);

    bmm.vBytes = ToByteVector(bytes);

    BOOST_CHECK(!bmm.IsBMMRequest());

    // Valid
    bmm.hashCritical = GetRandHash();
    BOOST_CHECK(bmm.IsBMMRequest());

    // Valid nSidechain 0 - 255 and check decoded return by reference)
    for (unsigned int i = 0; i < 256; i++) {
        bytes.clear();

        bytes.resize(4);
        bytes[0] = 0x00;
        bytes[1] = 0xbf;
        bytes[2] = 0x00;
        bytes[3] = uint8_t(i); // nSidechain
        bytes.push_back(0xFD);
        bytes.push_back(0xFD);
        bytes.push_back(0xFD);
        bytes.push_back(0xFD);

        bmm.vBytes = ToByteVector(bytes);

        uint8_t nSidechain;
        std::string strPrevBlock = "";
        BOOST_CHECK(bmm.IsBMMRequest(nSidechain, strPrevBlock));

        BOOST_CHECK(nSidechain == i);
        BOOST_CHECK(strPrevBlock == "fdfdfdfd");
    }


    // Invalid bytes - too few
    bytes.clear();

    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0); // nSidechain
    bytes.push_back(0xFD);
    bytes.push_back(0xFD);
    bytes.push_back(0xFD);

    bmm.vBytes = ToByteVector(bytes);

    BOOST_CHECK(!bmm.IsBMMRequest());

    // Invalid prev bytes - too many
    bytes.clear();

    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(255); // nSidechain
    bytes << ParseHex(HexStr(std::string("The Times 03/Jan/2009 Chancellor on brink of second bailout for banks")));

    bmm.vBytes = ToByteVector(bytes);

    BOOST_CHECK(!bmm.IsBMMRequest());
}

BOOST_AUTO_TEST_CASE(bmm_prevbytes_mempool)
{
    // Create a BMM h* request transaction
    // Create critical data
    CScript bytes;
    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0);

    CBlock block = CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());

    // Get the prevBlock hash
    std::string strPrevHash = chainActive.Tip()->GetBlockHash().ToString();
    strPrevHash = strPrevHash.substr(strPrevHash.size() - 8, strPrevHash.size() - 1);

    std::vector<unsigned char> vPrevBytes = ParseHex(strPrevHash);
    bytes.resize(8);
    memcpy(&bytes[4], vPrevBytes.data(), vPrevBytes.size());

    CCriticalData criticalData;
    criticalData.vBytes = ToByteVector(bytes);
    criticalData.hashCritical = GetRandHash();

    // Create transaction with critical data
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vin[0].prevout.hash = coinbaseTxns[0].GetHash();
    mtx.vin[0].prevout.n = 0;

    mtx.vout[0].scriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
    mtx.vout[0].nValue = 50 * CENT;

    // Set locktime to the block we would like critical data to be commited in
    mtx.nLockTime = 101;

    // Add critical data
    mtx.criticalData = criticalData;

    // Check that this is a valid BMM request first of all
    BOOST_CHECK(criticalData.IsBMMRequest());

    // Setup a keystore to hold the coinbase key
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKey(coinbaseKey);
    const CKeyStore& keystoreConst = tempKeystore;

    const CTransaction& txToSign = mtx;

    TransactionSignatureCreator creator(&keystoreConst, &txToSign, 0, coinbaseTxns[0].vout[0].nValue);

    SignatureData sigdata;
    bool sigCreated = ProduceSignature(creator, coinbaseTxns[0].vout[0].scriptPubKey, sigdata);
    BOOST_CHECK(sigCreated);

    mtx.vin[0].scriptSig = sigdata.scriptSig;

    CValidationState state;


    // Check that valid prevBytes are accepted to the mempool
    {
        LOCK(cs_main);
        BOOST_CHECK(AcceptToMemoryPool(mempool, state, MakeTransactionRef(mtx),
                    nullptr, nullptr, false, 0));
    }
    BOOST_CHECK(state.IsValid());

    // Remove the transaction we just added to the mempool before the next test
    mempool.removeRecursive(CTransaction(mtx));




    // Check that invalid (wrong block) prevBytes are rejected

    // Mine another block
    block = CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());

    // Update input
    mtx.vin.clear();
    mtx.vin.resize(1);
    mtx.vin[0].prevout.hash = coinbaseTxns[1].GetHash();
    mtx.vin[0].prevout.n = 0;

    // Update locktime
    mtx.nLockTime = 102;

    const CTransaction& txToSign2 = mtx;

    TransactionSignatureCreator creator2(&keystoreConst, &txToSign2, 0, coinbaseTxns[1].vout[0].nValue);

    SignatureData sigdata2;
    BOOST_CHECK(ProduceSignature(creator2, coinbaseTxns[1].vout[0].scriptPubKey, sigdata2));

    mtx.vin[0].scriptSig = sigdata2.scriptSig;

    CValidationState state2;

    // We haven't updated the prevBytes so they are outdated and invalid now,
    // confirm that this will be rejected from the memory pool.
    {
        LOCK(cs_main);
        BOOST_CHECK(!AcceptToMemoryPool(mempool, state2, MakeTransactionRef(mtx),
                    nullptr, nullptr, false, 0));
    }
    BOOST_CHECK(!state2.IsValid());

    // Remove the transaction we just added to the mempool before the next test
    mempool.removeRecursive(CTransaction(mtx));




    // Check that invalid (too many) prevBytes are rejected

    // Mine another block
    block = CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());

    bytes.clear();
    bytes.resize(3);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0);

    bytes << ParseHex(HexStr(std::string("trueno")));

    criticalData.vBytes = ToByteVector(bytes);

    // Update input
    mtx.vin.clear();
    mtx.vin.resize(1);
    mtx.vin[0].prevout.hash = coinbaseTxns[2].GetHash();
    mtx.vin[0].prevout.n = 0;

    // Update locktime
    mtx.nLockTime = 103;

    const CTransaction& txToSign3 = mtx;

    TransactionSignatureCreator creator3(&keystoreConst, &txToSign3, 0, coinbaseTxns[2].vout[0].nValue);

    SignatureData sigdata3;
    BOOST_CHECK(ProduceSignature(creator3, coinbaseTxns[2].vout[0].scriptPubKey, sigdata3));

    mtx.vin[0].scriptSig = sigdata3.scriptSig;

    CValidationState state3;

    // Check that a BMM request with too many prevBytes is rejected
    {
        LOCK(cs_main);
        BOOST_CHECK(!AcceptToMemoryPool(mempool, state3, MakeTransactionRef(mtx),
                    nullptr, nullptr, false, 0));
    }
    BOOST_CHECK(!state3.IsValid());


    // Remove the transaction we just added to the mempool before the next test
    mempool.removeRecursive(CTransaction(mtx));




    // Check that invalid (null) prevBytes are rejected

    // Mine another block
    block = CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());

    bytes.clear();
    bytes.resize(4);
    bytes[0] = 0x00;
    bytes[1] = 0xbf;
    bytes[2] = 0x00;
    bytes[3] = uint8_t(0);

    criticalData.vBytes = std::vector<unsigned char>(bytes.begin(), bytes.end());

    // Update input
    mtx.vin.clear();
    mtx.vin.resize(1);
    mtx.vin[0].prevout.hash = coinbaseTxns[3].GetHash();
    mtx.vin[0].prevout.n = 0;

    // Update locktime
    mtx.nLockTime = 104;

    const CTransaction& txToSign4 = mtx;

    TransactionSignatureCreator creator4(&keystoreConst, &txToSign4, 0, coinbaseTxns[3].vout[0].nValue);

    SignatureData sigdata4;
    BOOST_CHECK(ProduceSignature(creator4, coinbaseTxns[3].vout[0].scriptPubKey, sigdata4));

    mtx.vin[0].scriptSig = sigdata4.scriptSig;

    CValidationState state4;

    // Check that valid prevBytes are accepted to the mempool
    {
        LOCK(cs_main);
        BOOST_CHECK(!AcceptToMemoryPool(mempool, state4, MakeTransactionRef(mtx),
                    nullptr, nullptr, false, 0));
    }
    BOOST_CHECK(!state4.IsValid());

    // Remove the transaction we just added to the mempool before the next test
    mempool.removeRecursive(CTransaction(mtx));
}

BOOST_AUTO_TEST_SUITE_END()

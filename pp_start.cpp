#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <bitcoin/bitcoin.hpp>
#include "aes256.h"
using namespace bc;
namespace fs = boost::filesystem;

typedef std::vector<char> signed_data_chunk;

constexpr size_t maximum_msg_size = 1400;

hash_digest derive_seed(const ec_point& pubkey)
{
    data_chunk data(pubkey.begin(), pubkey.end());
    payment_address payaddr;
    set_public_key(payaddr, data);
    hash_digest result = null_hash;
    const short_hash& addr_hash = payaddr.hash();
    std::copy(addr_hash.begin(), addr_hash.end(), result.begin());
    return result;
}

// Encrypt for first hash.
data_chunk pp_encrypt(data_chunk buffer, hash_digest seed)
{
    aes256_context ctx; 
    BITCOIN_ASSERT(seed.size() == 32);
    aes256_init(&ctx, seed.data());
    BITCOIN_ASSERT(buffer.size() % 16 == 0);
    for (size_t i = 0; i < buffer.size(); i += 16)
        aes256_encrypt_ecb(&ctx, buffer.data() + i);
    aes256_done(&ctx);
    return buffer;
}

void test_decryption(const data_chunk& buffer,
    data_chunk cipher, const data_chunk& addr_pubkey)
{
    hash_digest seed = derive_seed(addr_pubkey);
    aes256_context ctx; 
    BITCOIN_ASSERT(seed.size() == 32);
    aes256_init(&ctx, seed.data());
    BITCOIN_ASSERT(cipher.size() % 16 == 0);
    for (size_t i = 0; i < cipher.size(); i += 16)
        aes256_decrypt_ecb(&ctx, cipher.data() + i);
    aes256_done(&ctx);
    BITCOIN_ASSERT(buffer == cipher);
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: pp_start DOCUMENT CHUNKS" << std::endl;
        std::cerr << "Good default for CHUNKS is 100" << std::endl;
        return -1;
    }
    const fs::path document_full_path = argv[1];
    const fs::path doc_path = document_full_path.parent_path();
    const fs::path doc_filename = document_full_path.filename();
    const std::string chunks_str = argv[2];
    size_t chunks = 0;
    try
    {
        chunks = boost::lexical_cast<size_t>(chunks_str);
    }
    catch (const boost::bad_lexical_cast&)
    {
        std::cerr << "pp_start: bad CHUNKS provided." << std::endl;
        return -1;
    }
    const fs::path public_chunks_path =
        doc_path / (doc_filename.native() + "_public_chunks");
#if 0
    if (!fs::create_directory(public_chunks_path))
    {
        std::cerr << "pp_start: error creating path '"
            << public_chunks_path.native() << "'" << std::endl;
        return -1;
    }
#endif
    std::ifstream infile(document_full_path.native(), std::ifstream::binary);
    infile.seekg(0, std::ifstream::end);
    size_t file_size = infile.tellg();
    infile.seekg(0, std::ifstream::beg);
    size_t chunk_size = file_size / chunks;
    // AES works on blocks of 16 bytes. Round up to nearest multiple.
    chunk_size += 16 - (chunk_size % 16);
    BITCOIN_ASSERT(chunk_size % 16 == 0);
    //std::cout << "Creating chunks of "
    //    << chunk_size << " bytes each." << std::endl;
    size_t i = 0;
    hash_digest_list hashes;
    while (infile)
    {
        data_chunk buffer(chunk_size);
        // Copy chunk to public chunk file.
        char* data = reinterpret_cast<char*>(buffer.data());
        infile.read(data, chunk_size);
        ++i;
        const fs::path chunk_filename =
            public_chunks_path / 
                (std::string("CHUNK.") + boost::lexical_cast<std::string>(i));
        std::ofstream outfile(chunk_filename.native(), std::ifstream::binary);
        // Create a seed.
        BITCOIN_ASSERT(ec_secret_size == hash_size);
        ec_secret secret = bitcoin_hash(buffer);
        ec_point pubkey = secret_to_public_key(secret);
        // Once we spend funds, we reveal the decryption pubkey.
        hash_digest seed = derive_seed(pubkey);
        // Should be encrypted!!
        // Use hash of pubkey as encryption key.
        data_chunk encrypted = pp_encrypt(buffer, seed);
        char* enc_data = reinterpret_cast<char*>(encrypted.data());
        outfile.write(enc_data, infile.gcount());
        test_decryption(buffer, encrypted, pubkey);
        // Write key also.
        // Add hash to list.
        hashes.push_back(seed);
    }
    std::cout << i << " chunks created." << std::endl;
    std::cout << "Choose a future block height, and announce this "
        "hash to the world:" << std::endl;
    std::cout << build_merkle_tree(hashes) << std::endl;
    return 0;
}

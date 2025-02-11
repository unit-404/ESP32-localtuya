#include "../openssl.hpp"
#include "../spake2p.hpp"

//
#ifdef ENABLE_PAKE_CLIENT
bool Spake2p::computeSecretAndVerifiersFromY(const BIGNUM* w1, const EC_POINT* X, const EC_POINT* Y,
std::vector<unsigned char>& Ke, std::vector<unsigned char>& hAY, std::vector<unsigned char>& hBX)
{
    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) return false;

    //
    EC_POINT* temp = EC_POINT_new(group);
    if (!EC_POINT_mul(group, temp, nullptr, N, w0, ctx)) {
        std::cerr << "EC_POINT_mul(N, w0) failed\n";
        BN_CTX_free(ctx); return false;
    }

    //
    if (!EC_POINT_invert(group, temp, ctx)) {
        std::cerr << "EC_POINT_invert failed\n";
        EC_POINT_free(temp); BN_CTX_free(ctx); return false;
    }

    //
    EC_POINT* Yprime = EC_POINT_new(group);
    if (!EC_POINT_add(group, Yprime, Y, temp, ctx)) {
        std::cerr << "EC_POINT_add (Y - N*w0) failed\n";
        EC_POINT_free(temp); EC_POINT_free(Yprime); BN_CTX_free(ctx); return false;
    }
    EC_POINT_free(temp);

    //
    EC_POINT* Z = EC_POINT_new(group);
    if (!EC_POINT_mul(group, Z, nullptr, Yprime, random, ctx)) {
        std::cerr << "EC_POINT_mul (Yprime*r) failed\n";
        EC_POINT_free(Yprime); BN_CTX_free(ctx); return false;
    }

    //
    EC_POINT* V = EC_POINT_new(group);
    if (!EC_POINT_mul(group, V, nullptr, Yprime, w1, ctx)) {
        std::cerr << "EC_POINT_mul (Yprime*w1) failed\n";
        EC_POINT_free(Yprime); EC_POINT_free(Z); BN_CTX_free(ctx); return false;
    }

    //
    unsigned char bufX[100] = {0}, bufY[100] = {0}, bufZ[100] = {0}, bufV[100] = {0};
    size_t lenX = EC_POINT_point2oct(group, X, POINT_CONVERSION_UNCOMPRESSED, bufX, sizeof(bufX), ctx);
    size_t lenY = EC_POINT_point2oct(group, Y, POINT_CONVERSION_UNCOMPRESSED, bufY, sizeof(bufY), ctx);
    size_t lenZ = EC_POINT_point2oct(group, Z, POINT_CONVERSION_UNCOMPRESSED, bufZ, sizeof(bufZ), ctx);
    size_t lenV = EC_POINT_point2oct(group, V, POINT_CONVERSION_UNCOMPRESSED, bufV, sizeof(bufV), ctx);

    //
    int w0_len = BN_num_bytes(w0);
    std::vector<unsigned char> w0_bytes(w0_len);
    BN_bn2bin(w0, w0_bytes.data());

    //
    std::vector<unsigned char> transcript;
    transcript.insert(transcript.end(), context.begin(), context.end());
    transcript.insert(transcript.end(), bufX, bufX + lenX);
    transcript.insert(transcript.end(), bufY, bufY + lenY);
    transcript.insert(transcript.end(), bufZ, bufZ + lenZ);
    transcript.insert(transcript.end(), bufV, bufV + lenV);
    transcript.insert(transcript.end(), w0_bytes.begin(), w0_bytes.end());

    //
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EC_POINT_free(Yprime); EC_POINT_free(Z); EC_POINT_free(V); BN_CTX_free(ctx); return false; }
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) { EVP_MD_CTX_free(mdctx); return false; }
    if (EVP_DigestUpdate(mdctx, transcript.data(), transcript.size()) != 1) { EVP_MD_CTX_free(mdctx); return false; }

    //
    unsigned int hash_len = 0; unsigned char hash[32] = {0};
    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) { EVP_MD_CTX_free(mdctx); return false; }
    EVP_MD_CTX_free(mdctx);

    //
    Ke.assign(hash+16, hash+32);
    hAY.assign(hash, hash + 16);
    hBX.assign(hash, hash + 16);

    //
    EC_POINT_free(Yprime);
    EC_POINT_free(Z);
    EC_POINT_free(V);
    BN_CTX_free(ctx);
    return true;
}
#endif

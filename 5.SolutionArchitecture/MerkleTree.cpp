#include <iostream>
#include <vector>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

using namespace std;
using uchar = unsigned char;

struct Node
{
    int NodeID;
    string value, hashValue;
    Node *leftchild, *rightchild, *parent;
};

class MerkleTree
{
    Node *root;
    int TreeSize, TreeNo, levels;
    const int HASHSIZE = 2 * SHA256_DIGEST_LENGTH;

    public:
    MerkleTree(int Tsize) : TreeSize(Tsize)
    {
        levels = 0;
        TreeNo = -1;
    }
    string SHA256(const string strIn)
    {
        uchar hash_buf[SHA256_DIGEST_LENGTH];
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();

        EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx, strIn.c_str(), strIn.size());
        EVP_DigestFinal_ex(ctx, hash_buf, NULL);
        EVP_MD_CTX_free(ctx);

        stringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) ss << hex <<setw(2) << setfill('0') << (int)hash_buf[i];
        return ss.str();
    }

    // this function receives the file name,
    // reads the text un chunks of the given size, and
    // builds the Merkle hash tree accordingly

    bool Build(const char* fileName)
    {
        return true;
    }
};

int main()
{
    return 0;
}
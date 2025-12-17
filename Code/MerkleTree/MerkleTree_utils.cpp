#include "MerkleTree.hpp"
#include <openssl/evp.h>
#include <iostream>
#include <iomanip>
#include <sstream>
std::string MerkleTree::SHA256(const std::string strIn)
{
    uchar hash_buf[SHA256_DIGEST_LENGTH];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();

    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, strIn.c_str(), strIn.size());
    EVP_DigestFinal_ex(ctx, hash_buf, NULL);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash_buf[i];
    return ss.str();
}

    
// Print the leaf values for test purposes
void MerkleTree::PrintTree()
{
    Traverse(root);
    std::cout << std::endl;
}

void MerkleTree::Traverse(const Node *tree)
{
    if(!tree) return;

    if(!tree->left)
    {
        std::cout << tree->blockID << "\t" <<tree->hashValue << std::endl;
        return;
    }
    Traverse(tree->left);
    Traverse(tree->right);
}

void MerkleTree::Traverse2(const Node* tree)
{
    if(!tree) return;
    Traverse2(tree->left);
    Traverse2(tree->right);
    std::cout << tree->hashValue << std::endl;
}
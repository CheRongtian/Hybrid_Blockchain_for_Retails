#ifndef MERKLETREE_HPP
#define MERKLETREE_HPP

#include <vector>
#include <openssl/sha.h>
#include <fstream>
#include <string>

using uchar = unsigned char;

struct Node
{
    int blockID;
    std::string value, hashValue;
    Node *left, *right, *parent;
};

class MerkleTree
{
    Node *root;
    int blockSize, blockNo;
    std::vector<std::string> blocks;
    const int HASHSIZE = 2 * SHA256_DIGEST_LENGTH;

    Node* BuildFromBlocks(const std::vector<std::string>& source);
    Node* CloneHashNode(const Node* source);
    Node* FindLeaf(Node* tree, int n) const;

    public:
        MerkleTree(int Tsize);

        // Free the whole tree when the obj has been destroyed
        ~MerkleTree();

        // Recursively free memory
        void Free(Node *t);

        std::string SHA256(const std::string strIn);

        // this function receives the file name,
        // reads the text un chunks of the given size, and
        // builds the Merkle hash tree accordingly

        bool Build(const char* fileName);

        // Print the leaf values for test purposes
        void PrintTree();

        void Traverse(const Node *tree);

        void Traverse2(const Node* tree);

        void UpdateHash(Node *leftN);


        // Append a new block into the tree
        bool Append(std::string strBlock);

        bool ReadBlock(int n);

        // When a block is requested, find it, and return it with a proof of membership
    
        std::string ProverBlock(int n);
    
         
        // Verify if the root' is the same as the Merkle Root
        bool Verify(std::string proof);
   
};

#endif

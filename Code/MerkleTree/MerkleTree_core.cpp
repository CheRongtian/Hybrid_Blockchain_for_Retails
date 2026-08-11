#include "MerkleTree.hpp"
MerkleTree::MerkleTree(int Tsize) : root(nullptr), blockSize(Tsize)
{
    blockNo = -1;
}

// Free the whole tree when the obj has been destroyed
MerkleTree::~MerkleTree()
{
    Free(root);
    root = nullptr;
}

std::string MerkleTree::GetRootHash() const
{
    return root ? root->hashValue : "";
}

int MerkleTree::GetBlockCount() const
{
    return static_cast<int>(blocks.size());
}

// Recursively free memory
void MerkleTree::Free(Node *t)
{
    if(!t) return;
    Free(t->left); // free left child
    Free(t->right); // free right child
    delete t;
}

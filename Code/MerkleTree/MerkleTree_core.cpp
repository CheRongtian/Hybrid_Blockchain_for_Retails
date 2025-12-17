#include "MerkleTree.hpp"
MerkleTree::MerkleTree(int Tsize) : blockSize(Tsize)
    {
        levels = 0;
        blockNo = -1;
    }

    // Free the whole tree when the obj has been destroyed
    MerkleTree::~MerkleTree()
    {
        Free(root);
        root = nullptr;
    }

    // Recursively free memory
    void MerkleTree::Free(Node *t)
    {
        if(!t) return;
        Free(t->left); // free left child
        Free(t->right); // free right child
        delete t;
    }
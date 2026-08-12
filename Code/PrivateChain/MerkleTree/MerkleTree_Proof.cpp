#include "MerkleTree.hpp"
#include <iostream>

// Find the requested leaf and return its membership proof.
std::string MerkleTree::ProverBlock(int n)
{
    if(!root || n < 0 || n > blockNo)
    {
        std::cout << "Not Found" << std::endl;
        return "";
    }

    Node *current = FindLeaf(root, n);
    if(!current)
    {
        std::cout << "Not Found" << std::endl;
        return "";
    }

    std::string proof;
    bool first = true;

    while(current->parent)
    {
        Node *parentN = current->parent;
        const bool currentIsLeft = parentN->left == current;
        Node *sibling = currentIsLeft ? parentN->right : parentN->left;

        if(!sibling)
        {
            std::cout << "Error: Invalid tree structure." << std::endl;
            return "";
        }

        if(first)
        {
            proof += currentIsLeft ? "L:" : "R:";
            proof += current->hashValue;
            first = false;
        }

        proof += currentIsLeft ? "R:" : "L:";
        proof += sibling->hashValue;
        current = parentN;
    }

    // A one-block tree has no siblings, but its leaf hash is still a valid proof.
    if(first)
    {
        proof = "L:";
        proof += current->hashValue;
    }

    return proof;
}

// Recompute a root from a proof and compare it with the current Merkle root.
bool MerkleTree::Verify(std::string proof)
{
    const std::size_t entrySize = 2 + static_cast<std::size_t>(HASHSIZE);
    if(!root || proof.size() < entrySize || proof.size() % entrySize != 0)
    {
        std::cout << "Error: Invalid or empty proof. Please Request (R) a block first."
                  << std::endl;
        return false;
    }

    std::size_t index = 0;
    std::string info = proof.substr(index, 2);
    if(info != "L:" && info != "R:")
    {
        std::cout << "Error: Invalid proof direction." << std::endl;
        return false;
    }

    index += 2;
    std::string strHash = proof.substr(index, HASHSIZE);
    index += HASHSIZE;

    while(index < proof.size())
    {
        info = proof.substr(index, 2);
        index += 2;

        if(info != "L:" && info != "R:")
        {
            std::cout << "Error: Invalid proof direction." << std::endl;
            return false;
        }

        const std::string siblingHash = proof.substr(index, HASHSIZE);
        index += HASHSIZE;

        if(info == "L:") strHash = SHA256(siblingHash + strHash);
        else strHash = SHA256(strHash + siblingHash);
    }

    const bool verified = strHash == root->hashValue;
    std::cout << "\n\n\t\t " << (verified ? "Verified" : "Not Verified")
              << std::endl << std::endl;
    return verified;
}

bool MerkleTree::ReadBlock(int n)
{
    if(!root || n < 0 || n > blockNo)
    {
        std::cout << "Not Found" << std::endl;
        return false;
    }

    Node *leaf = FindLeaf(root, n);
    if(!leaf)
    {
        std::cout << "Not Found" << std::endl;
        return false;
    }

    std::cout << "Found: " << leaf->hashValue << std::endl;
    return true;
}

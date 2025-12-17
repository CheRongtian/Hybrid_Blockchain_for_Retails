#include "MerkleTree.hpp"
#include <iostream>

// When a block is requested, find it, and return it with a proof of membership
std::string MerkleTree::ProverBlock(int n)
{
    std::string proof("");
    if(n > blockNo)
    {
        std::cout << "Not Found" << std::endl;
        return proof;
    }

    int tempN = n;
    Node *tree = root;
    for(int i = levels; i>=0; i--)
    {
        if(tempN < pow(2, i-1))
        {
            if(tree->left) tree = tree->left; // Go left
            else std::cout << "Found: " << tree->hashValue << std::endl;
        }

        else
        {
            tempN -= pow(2, i-1);
            if(tree->right) tree = tree->right;
            else std::cout << "Found: " << tree->hashValue << std::endl;
        }
    }

    // Now prepare the proof
    bool bFirst = true;
    Node *leftN = tree, *parentN = leftN->parent, *rightN = nullptr;
    while(parentN)
    {
        if(parentN->left == leftN)
        {
            rightN = parentN->right;
            if(bFirst)
            {
                proof += "L:";
                proof += leftN->hashValue;
                bFirst = false;
            }
            proof += "R:";
            proof += rightN->hashValue;
        }

        else
        {
            rightN = leftN;
            leftN = parentN->left;
            if(bFirst)
            {
                proof += "R:";
                proof += rightN->hashValue;
                bFirst = false;
            }
            proof += "L:";
            proof += leftN->hashValue;
        }
        leftN = parentN;
        parentN = parentN->parent;
    }
    return proof;
}

// Verify if the root' is the same as the Merkle Root
bool MerkleTree::Verify(std::string proof)
{
    if (proof.length() < HASHSIZE) 
    {
        std::cout << "Error: Invalid or empty proof. Please Request (R) a block first." << std::endl;
        return false;
    }

    std::string info(""), strHash(""), strTemp("");
    int index = 0, n = proof.length();
    // Read the first 2 characters
    info = proof.substr(0, 2);
    index = proof.find((info=="L:") ? "R:" : "L:", 2);
    strTemp = proof.substr(2, (index == std::string::npos) ? (n - 2): (index - 2));
    strHash = strTemp;

    while(index < n)
    {
        info = proof.substr(index, 2);
        index += 2;
        strTemp = proof.substr(index, HASHSIZE);
            
        if(info == "L:") strHash = SHA256(strTemp + strHash);
        else strHash = SHA256(strHash + strTemp);

        index += HASHSIZE;
    }

    if(strHash == root->hashValue) std::cout << "\n\n\t\t Verified" << std::endl << std::endl;
    else std::cout << "\n\n\t\t Not Verified" << std::endl << std::endl;

    return (strHash == root->hashValue);
}

bool MerkleTree::ReadBlock(int n)
{
    if(n > blockNo)
    {
        std::cout << "Not Found" << std::endl;
        return false;
    }

    int tempN = n;
    Node * tree = root;
    for(int i = levels; i>=0; i--)
    {
        if(tempN < pow(2, i-1))
        {
            if(tree->left) tree = tree->left; // Go left
            else
            {
                std::cout << "Found: " << tree->hashValue << std::endl;
                return true;
            }
        }

        else
        {
            tempN -= pow(2, i-1);
            if(tree->right) tree = tree->right;
            else
            {
                std::cout << "Found: " << tree->hashValue << std::endl;
                return true;
            }
        }
    }

    return false;
}
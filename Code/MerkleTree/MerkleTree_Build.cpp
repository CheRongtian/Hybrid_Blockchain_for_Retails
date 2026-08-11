#include "MerkleTree.hpp"
#include <fstream>
#include <iostream>
#include <utility>

Node* MerkleTree::CloneHashNode(const Node* source)
{
    Node *duplicate = new Node{};
    duplicate->blockID = -1;
    duplicate->value = "";
    duplicate->hashValue = source->hashValue;
    duplicate->left = duplicate->right = duplicate->parent = nullptr;
    return duplicate;
}

Node* MerkleTree::BuildFromBlocks(const std::vector<std::string>& source)
{
    if(source.empty()) return nullptr;

    std::vector<Node*> current;
    current.reserve(source.size());

    for(std::size_t i = 0; i < source.size(); i++)
    {
        Node *leaf = new Node{};
        leaf->blockID = static_cast<int>(i);
        leaf->value = source[i];
        leaf->hashValue = SHA256(leaf->value);
        leaf->left = leaf->right = leaf->parent = nullptr;
        current.push_back(leaf);
    }

    while(current.size() > 1)
    {
        std::vector<Node*> next;
        next.reserve((current.size() + 1) / 2);

        for(std::size_t i = 0; i < current.size(); i += 2)
        {
            Node *leftN = current[i];
            Node *rightN = (i + 1 < current.size())
                ? current[i + 1]
                : CloneHashNode(leftN);

            Node *parentN = new Node{};
            parentN->blockID = -1;
            parentN->value = "";
            parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
            parentN->left = leftN;
            parentN->right = rightN;
            parentN->parent = nullptr;

            leftN->parent = parentN;
            rightN->parent = parentN;
            next.push_back(parentN);
        }

        current.swap(next);
    }

    return current.front();
}

Node* MerkleTree::FindLeaf(Node* tree, int n) const
{
    if(!tree) return nullptr;

    if(!tree->left && !tree->right)
        return tree->blockID == n ? tree : nullptr;

    if(Node *found = FindLeaf(tree->left, n)) return found;
    return FindLeaf(tree->right, n);
}

// this function receives the file name,
// reads the text in chunks of the given size, and
// builds the Merkle hash tree accordingly
bool MerkleTree::Build(const char* fileName)
{
    if(!fileName || blockSize <= 0) return false;

    std::ifstream inFile(fileName, std::ios::binary);
    if(!inFile.good()) return false;

    std::vector<std::string> newBlocks;
    std::vector<char> buffer(static_cast<std::size_t>(blockSize));

    while(true)
    {
        inFile.read(buffer.data(), blockSize);
        const std::streamsize count = inFile.gcount();
        if(count <= 0) break;

        if(count < blockSize)
        {
            for(std::streamsize i = count; i < blockSize; i++)
                buffer[static_cast<std::size_t>(i)] = 'E';
        }

        newBlocks.emplace_back(buffer.data(), static_cast<std::size_t>(blockSize));
        if(count < blockSize) break;
    }

    if(newBlocks.empty()) return false;

    Node *newRoot = BuildFromBlocks(newBlocks);
    if(!newRoot) return false;

    Free(root);
    blocks.swap(newBlocks);
    root = newRoot;
    blockNo = static_cast<int>(blocks.size()) - 1;
    return true;
}

// Append a new block into the tree.
// The tree is rebuilt so the duplicate-last-hash rule is applied consistently
// at every level after an append.
bool MerkleTree::Append(std::string strBlock)
{
    if(strBlock.empty())
    {
        std::cout << "Error: Empty block" << std::endl;
        return false;
    }

    std::vector<std::string> newBlocks = blocks;
    newBlocks.push_back(std::move(strBlock));

    Node *newRoot = BuildFromBlocks(newBlocks);
    if(!newRoot) return false;

    Free(root);
    blocks.swap(newBlocks);
    root = newRoot;
    blockNo = static_cast<int>(blocks.size()) - 1;
    return true;
}

void MerkleTree::UpdateHash(Node *leftN)
{
    if(!leftN) return;

    Node *parentN = leftN->parent;
    while(parentN)
    {
        Node *rightN = nullptr;
        if(parentN->left == leftN)
        {
            rightN = parentN->right;
            // A duplicate node represents the current left subtree's hash.
            if(rightN && !rightN->left && !rightN->right && rightN->blockID == -1)
                rightN->hashValue = leftN->hashValue;
        }
        else
        {
            rightN = leftN;
            leftN = parentN->left;
        }

        if(!leftN || !rightN) return;
        parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
        leftN = parentN;
        parentN = parentN->parent;
    }
}

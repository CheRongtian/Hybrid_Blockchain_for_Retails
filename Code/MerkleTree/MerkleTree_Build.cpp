#include "MerkleTree.hpp"
#include <fstream>
#include <iostream>
#include <cmath>

// this function receives the file name,
// reads the text un chunks of the given size, and
// builds the Merkle hash tree accordingly

bool MerkleTree::Build(const char* fileName)
{
    std::ifstream inFile(fileName);
    if(!inFile.good()) return false;

    // Now, read the file block by block of the given size.
    // And, add all blocks into a list.
    std::vector<Node*> listChild, listParent;
    Node *leftN = nullptr, *rightN = nullptr;
    char buffer[blockSize + 1] ;
    buffer[blockSize] = '\0';
    while(!inFile.eof())
    {
        inFile.read(buffer, blockSize);
        if(!inFile.gcount()) continue;
        if(inFile.gcount() < blockSize)
        {
            for(int i=inFile.gcount(); i<blockSize; i++) buffer[i] = 'E';
        }
        Node *n = new Node;
        n->value = buffer;
        n->hashValue = SHA256(buffer);
        n->blockID = ++blockNo;
        n->left = n->right = n->parent = nullptr;
        listChild.push_back(n);
    }

    do
    {
        levels++;
    }while(pow(2, levels) <= blockNo);

    // And, build the tree over the blocks stored in the list
    do
    {
        auto iterP = listParent.begin();
        auto doneP = listParent.end();
        while(iterP != doneP) listChild.push_back(*iterP++);
        listParent.clear();

        auto iter = listChild.begin();
        auto done = listChild.end();
        leftN = rightN = nullptr;
        while(iter != done)
        {
            if(!leftN) leftN = *iter++;
            else
            {
                rightN = *iter++;
                Node *parentN = new Node;
                parentN->value = leftN->value;
                parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
                parentN->left = leftN;
                parentN->right = rightN;
                parentN->parent = nullptr;
                parentN->blockID = -1;
                listParent.push_back(parentN);
                leftN->parent = rightN->parent = parentN;
                leftN = rightN = nullptr;
            }
        }

        if(leftN)
        {
            // Make a new NULL node
            rightN = new Node;
            rightN->hashValue = "";
            rightN->blockID = -1;
            rightN->left = rightN->right = nullptr;

            Node *parentN = new Node;
            parentN->value = leftN->value;
            parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
            parentN->left = leftN;
            parentN->right = rightN;
            parentN->parent = nullptr;
            parentN->blockID = -1;
            listParent.push_back(parentN);
            leftN->parent = rightN->parent = parentN;
            leftN = rightN = nullptr;
        }
            
        listChild.clear();
    }while(listParent.size() > 1);

    root = listParent[0];
    return true;
}

// Append a new block into the tree
bool MerkleTree::Append(std::string strBlock)
{
    if(!strBlock.length())
    {
        std::cout << "Error: Empty block" << std::endl;
        return false;
    }

    blockNo++;
    Node *parentN = nullptr, *leftN = nullptr, *rightN = nullptr;
           
    // if it is a complete tree, add a new subtree and add one more level.
    if(blockNo == pow(2, levels))
    {
        std::cout << "1: blockNo: " << blockNo << "\tlevels: " << levels << std::endl;
        // Make a new root node.
        parentN = new Node;
        parentN->value = parentN->hashValue = "";
        parentN->blockID = -1;
        parentN->left = root;
        parentN->right = parentN->parent = nullptr;
        root->parent = parentN;
        root = parentN;

        parentN = new Node;
        parentN->value = parentN->hashValue = "";
        parentN->blockID = -1;
        parentN->left = parentN->right = nullptr;
        parentN->parent = root;
        // root->parent = parentN;
        root->right = parentN;

        // Make levels number of null nodes to make a new path
        for(int i = 0; i < levels; i++)
        {
            leftN = new Node;
            leftN->value = "";
            leftN->hashValue = SHA256("NULL");
            leftN->blockID = -1;
            leftN->left = leftN->right = nullptr;

            rightN = new Node;
            rightN->blockID = -1;
            rightN->value = "";
            rightN->hashValue = SHA256("NULL");
            rightN->left = rightN->right = nullptr;
                
            parentN->left = leftN;
            parentN->right = rightN;
            leftN->parent = rightN->parent = parentN;
            parentN = leftN;
        }
            
        leftN->blockID = blockNo;
        leftN->hashValue = SHA256(strBlock);
        levels++;
    }

    else
    {
        parentN = root;
        int tempN = blockNo;
        if(blockNo % 2)
        {
            std::cout << "2: blockNo: " << blockNo << std::endl;
            for(int i=levels-1; i>=0; i--)
            {
                if(!parentN)
                {
                    std::cout << "Error: Tree structure mismatch: nullptr. " << std::endl;
                    return false;
                }

                if(tempN < pow(2, i)) parentN = parentN->left; // Go left
                else
                {
                    tempN -= pow(2, i);
                    if (!parentN->right) 
                    {
                        std::cout << "Error: Path not found." << std::endl;
                        return false;
                    }
                    parentN = parentN->right;
                }
            }

            leftN = parentN;
            leftN->blockID = blockNo;
            leftN->hashValue = SHA256(strBlock);
        }

        else
        {
            std::cout << "3: blockNo: " << blockNo << std::endl;
            int i = levels;
            for(; i >=0; i--)
            {
                if(tempN < pow(2, i-1)) 
                {
                    if(parentN->left) parentN = parentN->left; // Go left
                    else break;
                }

                else
                {
                    tempN -= pow(2, i-1);
                    if(parentN->right) parentN = parentN->right;
                    else break;
                }
            }
            
            // Now add the required part of the path
            while(i>0)
            {
                leftN = new Node;
                leftN->blockID = -1;
                leftN->value = leftN->hashValue = "";
                leftN->left = leftN->right = nullptr;

                rightN = new Node;
                rightN->blockID = -1;
                rightN->value = "";
                rightN->hashValue = SHA256("NULL");
                rightN->left = rightN->right = nullptr;
                
                parentN->left = leftN;
                parentN->right = rightN;
                leftN->parent = rightN->parent = parentN;
                parentN = leftN;
                i--;
            }
            leftN->blockID = blockNo;
            leftN->hashValue = SHA256(strBlock);
        }
    }

    // Now update the hash values from bottom up
    UpdateHash(leftN);
    return false;
}

void MerkleTree::UpdateHash(Node *leftN)
{
    Node *parentN = leftN->parent, *rightN = nullptr;
    while(parentN)
    {
        if(parentN->left == leftN) rightN = parentN->right;
        else
        {
            rightN = leftN;
            leftN = parentN->left;
        }

        parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
        leftN = parentN;
        parentN = parentN->parent;
    }
}